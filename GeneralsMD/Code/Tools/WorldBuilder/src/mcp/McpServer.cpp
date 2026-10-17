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

// McpServer.cpp
// Localhost TCP command server that lets an external MCP server drive WorldBuilder.

#include "StdAfx.h"
#include <winsock2.h>
#include <process.h>

#include "mcp/McpServer.h"
#include "mcp/McpCommands.h"
#include "Common/Debug.h"

#include <deque>
#include <memory>
#include <stdlib.h>
#include <string>

namespace
{

const UINT WM_MCP_COMMAND = WM_APP + 0x4D;
const DWORD COMMAND_TIMEOUT_MS = 120000;
const size_t MAX_LINE_LENGTH = 32 * 1024 * 1024;
const char *const WINDOW_CLASS_NAME = "WorldBuilderMcpBridge";

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

HWND s_window = nullptr;
HANDLE s_thread = nullptr;
HANDLE s_stopEvent = nullptr;
volatile LONG s_stopping = 0;
SOCKET s_listenSocket = INVALID_SOCKET;
SOCKET s_clientSocket = INVALID_SOCKET;
int s_port = 0;
bool s_inDispatch = false;

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

/// Runs on the UI thread.
void drainQueue()
{
	// A command can pump messages (e.g. while loading a map), so don't recurse into the queue.
	if (s_inDispatch) {
		return;
	}
	s_inDispatch = true;
	for (;;) {
		RequestPtr req = popRequest();
		if (!req) {
			break;
		}
		req->reply = mcpDispatch(req->line);
		::SetEvent(req->done);
	}
	s_inDispatch = false;
}

LRESULT CALLBACK bridgeWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_MCP_COMMAND) {
		drainQueue();
		return 0;
	}
	return ::DefWindowProc(hwnd, msg, wParam, lParam);
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
	reply.set("id", id).set("ok", false).set("error", "timed out waiting for WorldBuilder to run the command");
	return reply.dump();
}

/// Hands one request line to the UI thread and waits for its reply.
std::string runOnUiThread(const std::string &line)
{
	RequestPtr req(new Request);
	req->line = line;
	::EnterCriticalSection(&s_queueLock);
	s_queue.push_back(req);
	::LeaveCriticalSection(&s_queueLock);
	::PostMessage(s_window, WM_MCP_COMMAND, 0, 0);

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
			std::string reply = runOnUiThread(line);
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
		s_clientSocket = client;
		serveClient(client);
		s_clientSocket = INVALID_SOCKET;
		::closesocket(client);
	}
	return 0;
}

} // namespace

int McpServer::getRequestedPort()
{
	int port = 0;
	for (int i = 1; i < __argc; i++) {
		const char *arg = __argv[i];
		if (_stricmp(arg, "-mcp") == 0 || _stricmp(arg, "/mcp") == 0) {
			if (port == 0) {
				port = DEFAULT_PORT;
			}
		} else if (_strnicmp(arg, "-mcpport:", 9) == 0 || _strnicmp(arg, "-mcpport=", 9) == 0) {
			port = atoi(arg + 9);
		}
	}
	if (port == 0) {
		const char *env = getenv("WB_MCP_PORT");
		if (env != nullptr && *env) {
			port = atoi(env);
		}
	}
	if (port < 0 || port > 65535) {
		port = 0;
	}
	return port;
}

bool McpServer::start(int port)
{
	if (s_thread != nullptr) {
		return true;
	}
	if (!s_queueLockInited) {
		::InitializeCriticalSection(&s_queueLock);
		s_queueLockInited = true;
	}

	WNDCLASS wc;
	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = bridgeWindowProc;
	wc.hInstance = AfxGetInstanceHandle();
	wc.lpszClassName = WINDOW_CLASS_NAME;
	::RegisterClass(&wc);
	s_window = ::CreateWindow(WINDOW_CLASS_NAME, "", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
	if (s_window == nullptr) {
		DEBUG_LOG(("MCP bridge: could not create message window"));
		return false;
	}

	WSADATA wsaData;
	if (::WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		DEBUG_LOG(("MCP bridge: WSAStartup failed"));
		::DestroyWindow(s_window);
		s_window = nullptr;
		return false;
	}

	s_listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((u_short)port);
	// Loopback only: the bridge can edit and save files, so it must never be reachable from the network.
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (s_listenSocket == INVALID_SOCKET
		|| ::bind(s_listenSocket, (sockaddr *)&addr, sizeof(addr)) != 0
		|| ::listen(s_listenSocket, 1) != 0) {
		DEBUG_LOG(("MCP bridge: could not listen on 127.0.0.1:%d (error %d)", port, ::WSAGetLastError()));
		if (s_listenSocket != INVALID_SOCKET) {
			::closesocket(s_listenSocket);
			s_listenSocket = INVALID_SOCKET;
		}
		::WSACleanup();
		::DestroyWindow(s_window);
		s_window = nullptr;
		return false;
	}

	s_stopping = 0;
	s_stopEvent = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
	s_port = port;
	s_thread = (HANDLE)_beginthreadex(nullptr, 0, serverThread, nullptr, 0, nullptr);
	if (s_thread == nullptr) {
		stop();
		return false;
	}
	DEBUG_LOG(("MCP bridge: listening on 127.0.0.1:%d", port));
	return true;
}

void McpServer::stop()
{
	if (s_window == nullptr) {
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
	if (s_listenSocket != INVALID_SOCKET) {
		::closesocket(s_listenSocket);
		s_listenSocket = INVALID_SOCKET;
	}
	::WSACleanup();
	if (s_stopEvent != nullptr) {
		::CloseHandle(s_stopEvent);
		s_stopEvent = nullptr;
	}
	::EnterCriticalSection(&s_queueLock);
	s_queue.clear();
	::LeaveCriticalSection(&s_queueLock);
	::DestroyWindow(s_window);
	s_window = nullptr;
	s_port = 0;
}

bool McpServer::isRunning()
{
	return s_thread != nullptr;
}
