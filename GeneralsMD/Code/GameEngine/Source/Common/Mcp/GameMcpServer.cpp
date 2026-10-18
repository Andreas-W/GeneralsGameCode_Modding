/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// GameMcpServer.cpp
// Localhost TCP transport of the game's MCP bridge. A socket thread reads request lines and queues
// them; the main thread runs them in gameMcpServerDrain() and the socket thread sends the replies.
// Kept in its own file because <winsock2.h> must not meet the <winsock.h> of the network code.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#ifdef RTS_GAME_MCP

#include <winsock2.h>
#include <process.h>

#include "GameMcpCommands.h"

#include <deque>
#include <memory>

namespace
{

// Long enough for a map to load while a command waits behind it.
const DWORD COMMAND_TIMEOUT_MS = 300000;
const size_t MAX_LINE_LENGTH = 16 * 1024 * 1024;

struct Request
{
	Request() : done(::CreateEvent(nullptr, TRUE, FALSE, nullptr)) {}
	~Request() { ::CloseHandle(done); }

	std::string line;
	std::string reply;
	HANDLE done;
};

typedef std::shared_ptr<Request> RequestPtr;

CRITICAL_SECTION s_queueLock;
bool s_queueLockInited = false;
std::deque<RequestPtr> s_queue;

HANDLE s_thread = nullptr;
HANDLE s_stopEvent = nullptr;
volatile LONG s_stopping = 0;
SOCKET s_listenSocket = INVALID_SOCKET;
bool s_inDrain = false;

RequestPtr popRequest()
{
	RequestPtr req;
	::EnterCriticalSection(&s_queueLock);
	if (!s_queue.empty()) {
		req = s_queue.front();
		s_queue.pop_front();
	}
	::LeaveCriticalSection(&s_queueLock);
	return req;
}

bool sendAll(SOCKET sock, const std::string &data)
{
	size_t sent = 0;
	while (sent < data.size()) {
		int n = ::send(sock, data.c_str() + sent, (int)(data.size() - sent), 0);
		if (n <= 0) {
			return false;
		}
		sent += n;
	}
	return true;
}

std::string timeoutReply(const std::string &line)
{
	McpJson request;
	std::string err;
	McpJson id;
	if (McpJson::parse(line, request, err)) {
		id = request.get("id");
	}
	McpJson reply = McpJson::makeObject();
	reply.set("id", id).set("ok", false).set("error", "timed out waiting for the game to run the command");
	return reply.dump();
}

/// Queues one request line for the main thread and waits for its reply.
std::string runOnMainThread(const std::string &line)
{
	RequestPtr req(new Request);
	req->line = line;
	::EnterCriticalSection(&s_queueLock);
	s_queue.push_back(req);
	::LeaveCriticalSection(&s_queueLock);

	HANDLE handles[2] = { req->done, s_stopEvent };
	DWORD result = ::WaitForMultipleObjects(2, handles, FALSE, COMMAND_TIMEOUT_MS);
	if (result == WAIT_OBJECT_0) {
		return req->reply;
	}
	return timeoutReply(line);
}

void serveClient(SOCKET client)
{
	std::string buffer;
	char chunk[8192];
	while (!s_stopping) {
		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(client, &readSet);
		timeval tv = { 0, 250000 };
		int ready = ::select(0, &readSet, nullptr, nullptr, &tv);
		if (ready < 0) {
			return;
		}
		if (ready == 0) {
			continue;
		}
		int n = ::recv(client, chunk, sizeof(chunk), 0);
		if (n <= 0) {
			return;
		}
		buffer.append(chunk, n);
		if (buffer.size() > MAX_LINE_LENGTH) {
			return;
		}
		size_t newline;
		while ((newline = buffer.find('\n')) != std::string::npos) {
			std::string line = buffer.substr(0, newline);
			buffer.erase(0, newline + 1);
			if (!line.empty() && line[line.size() - 1] == '\r') {
				line.erase(line.size() - 1);
			}
			if (line.empty()) {
				continue;
			}
			std::string reply = runOnMainThread(line);
			reply += '\n';
			if (!sendAll(client, reply)) {
				return;
			}
		}
	}
}

unsigned __stdcall serverThread(void *)
{
	while (!s_stopping) {
		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(s_listenSocket, &readSet);
		timeval tv = { 0, 250000 };
		int ready = ::select(0, &readSet, nullptr, nullptr, &tv);
		if (ready <= 0) {
			continue;
		}
		SOCKET client = ::accept(s_listenSocket, nullptr, nullptr);
		if (client == INVALID_SOCKET) {
			continue;
		}
		serveClient(client);
		::closesocket(client);
	}
	return 0;
}

} // namespace

//-------------------------------------------------------------------------------------------------
bool gameMcpServerStart(int port)
{
	if (s_thread != nullptr) {
		return true;
	}
	if (!s_queueLockInited) {
		::InitializeCriticalSection(&s_queueLock);
		s_queueLockInited = true;
	}

	WSADATA wsaData;
	if (::WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		DEBUG_LOG(("Game MCP bridge: WSAStartup failed"));
		return false;
	}

	s_listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((u_short)port);
	// Loopback only: the bridge can change the game at will, so it must never be reachable from the network.
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (s_listenSocket == INVALID_SOCKET
		|| ::bind(s_listenSocket, (sockaddr *)&addr, sizeof(addr)) != 0
		|| ::listen(s_listenSocket, 1) != 0) {
		DEBUG_LOG(("Game MCP bridge: could not listen on 127.0.0.1:%d (error %d)", port, ::WSAGetLastError()));
		if (s_listenSocket != INVALID_SOCKET) {
			::closesocket(s_listenSocket);
			s_listenSocket = INVALID_SOCKET;
		}
		::WSACleanup();
		return false;
	}

	s_stopping = 0;
	s_stopEvent = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
	s_thread = (HANDLE)_beginthreadex(nullptr, 0, serverThread, nullptr, 0, nullptr);
	if (s_thread == nullptr) {
		gameMcpServerStop();
		return false;
	}
	DEBUG_LOG(("Game MCP bridge: listening on 127.0.0.1:%d", port));
	return true;
}

//-------------------------------------------------------------------------------------------------
void gameMcpServerStop()
{
	if (s_listenSocket == INVALID_SOCKET) {
		return;
	}
	::InterlockedExchange(&s_stopping, 1);
	if (s_stopEvent != nullptr) {
		::SetEvent(s_stopEvent);
	}
	if (s_thread != nullptr) {
		::WaitForSingleObject(s_thread, 5000);
		::CloseHandle(s_thread);
		s_thread = nullptr;
	}
	::closesocket(s_listenSocket);
	s_listenSocket = INVALID_SOCKET;
	::WSACleanup();
	if (s_stopEvent != nullptr) {
		::CloseHandle(s_stopEvent);
		s_stopEvent = nullptr;
	}
	::EnterCriticalSection(&s_queueLock);
	s_queue.clear();
	::LeaveCriticalSection(&s_queueLock);
}

//-------------------------------------------------------------------------------------------------
void gameMcpServerDrain()
{
	// A command can run the engine (e.g. while a map loads), which polls again; don't recurse.
	if (s_inDrain || !s_queueLockInited) {
		return;
	}
	s_inDrain = true;
	for (;;) {
		RequestPtr req = popRequest();
		if (!req) {
			break;
		}
		req->reply = gameMcpDispatch(req->line);
		::SetEvent(req->done);
	}
	s_inDrain = false;
}

#endif // RTS_GAME_MCP
