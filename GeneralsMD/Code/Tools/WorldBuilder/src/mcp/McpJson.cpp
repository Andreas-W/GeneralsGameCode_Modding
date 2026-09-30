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

// McpJson.cpp
// Minimal JSON value used by the WorldBuilder MCP command bridge.

#include "StdAfx.h"
#include "mcp/McpJson.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static const McpJson s_nullJson;

size_t McpJson::size() const
{
	if (m_type == TYPE_ARRAY || m_type == TYPE_OBJECT) {
		return m_values.size();
	}
	return 0;
}

const McpJson &McpJson::at(size_t ndx) const
{
	if (m_type != TYPE_ARRAY || ndx >= m_values.size()) {
		return s_nullJson;
	}
	return m_values[ndx];
}

McpJson &McpJson::push(const McpJson &value)
{
	if (m_type != TYPE_ARRAY) {
		*this = makeArray();
	}
	m_values.push_back(value);
	return m_values.back();
}

bool McpJson::has(const char *key) const
{
	if (m_type != TYPE_OBJECT) {
		return false;
	}
	for (size_t i = 0; i < m_keys.size(); i++) {
		if (m_keys[i] == key) {
			return !m_values[i].isNull();
		}
	}
	return false;
}

const McpJson &McpJson::get(const char *key) const
{
	if (m_type == TYPE_OBJECT) {
		for (size_t i = 0; i < m_keys.size(); i++) {
			if (m_keys[i] == key) {
				return m_values[i];
			}
		}
	}
	return s_nullJson;
}

McpJson &McpJson::set(const char *key, const McpJson &value)
{
	if (m_type != TYPE_OBJECT) {
		*this = makeObject();
	}
	for (size_t i = 0; i < m_keys.size(); i++) {
		if (m_keys[i] == key) {
			m_values[i] = value;
			return *this;
		}
	}
	m_keys.push_back(key);
	m_values.push_back(value);
	return *this;
}

//-------------------------------------------------------------------------------------------------
// Serialization
//-------------------------------------------------------------------------------------------------

static void dumpString(std::string &out, const std::string &s)
{
	out += '"';
	for (size_t i = 0; i < s.size(); i++) {
		unsigned char c = (unsigned char)s[i];
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			default:
				if (c < 0x20) {
					char buf[8];
					sprintf(buf, "\\u%04x", c);
					out += buf;
				} else if (c >= 0x80) {
					// Game strings are Windows-1252, map them to their code point so the output stays valid UTF-8 JSON.
					char buf[8];
					sprintf(buf, "\\u%04x", c);
					out += buf;
				} else {
					out += (char)c;
				}
				break;
		}
	}
	out += '"';
}

void McpJson::dumpTo(std::string &out) const
{
	switch (m_type) {
		case TYPE_NULL:
			out += "null";
			break;
		case TYPE_BOOL:
			out += m_bool ? "true" : "false";
			break;
		case TYPE_NUMBER: {
			char buf[64];
			if (!_finite(m_number)) {
				out += "null";
			} else if (m_number == floor(m_number) && fabs(m_number) < 1e15) {
				sprintf(buf, "%.0f", m_number);
				out += buf;
			} else {
				sprintf(buf, "%.9g", m_number);
				out += buf;
			}
			break;
		}
		case TYPE_STRING:
			dumpString(out, m_string);
			break;
		case TYPE_ARRAY:
			out += '[';
			for (size_t i = 0; i < m_values.size(); i++) {
				if (i > 0) out += ',';
				m_values[i].dumpTo(out);
			}
			out += ']';
			break;
		case TYPE_OBJECT:
			out += '{';
			for (size_t i = 0; i < m_keys.size(); i++) {
				if (i > 0) out += ',';
				dumpString(out, m_keys[i]);
				out += ':';
				m_values[i].dumpTo(out);
			}
			out += '}';
			break;
	}
}

std::string McpJson::dump() const
{
	std::string out;
	dumpTo(out);
	return out;
}

//-------------------------------------------------------------------------------------------------
// Parsing
//-------------------------------------------------------------------------------------------------

namespace
{

class JsonParser
{
public:
	JsonParser(const std::string &text) : m_text(text), m_pos(0) {}

	bool parseDocument(McpJson &out, std::string &error)
	{
		if (!parseValue(out, 0)) {
			error = m_error;
			return false;
		}
		skipSpace();
		if (m_pos != m_text.size()) {
			error = "trailing characters after JSON value";
			return false;
		}
		return true;
	}

private:
	enum { MAX_DEPTH = 64 };

	const std::string &m_text;
	size_t m_pos;
	std::string m_error;

	bool fail(const char *msg)
	{
		char buf[128];
		sprintf(buf, "%s at offset %u", msg, (unsigned)m_pos);
		m_error = buf;
		return false;
	}

	void skipSpace()
	{
		while (m_pos < m_text.size()) {
			char c = m_text[m_pos];
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
				m_pos++;
			} else {
				break;
			}
		}
	}

	bool match(const char *word)
	{
		size_t len = strlen(word);
		if (m_text.compare(m_pos, len, word) == 0) {
			m_pos += len;
			return true;
		}
		return false;
	}

	static void appendUtf8(std::string &out, unsigned int cp)
	{
		if (cp < 0x80) {
			out += (char)cp;
		} else if (cp < 0x800) {
			out += (char)(0xC0 | (cp >> 6));
			out += (char)(0x80 | (cp & 0x3F));
		} else if (cp < 0x10000) {
			out += (char)(0xE0 | (cp >> 12));
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		} else {
			out += (char)(0xF0 | (cp >> 18));
			out += (char)(0x80 | ((cp >> 12) & 0x3F));
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		}
	}

	bool parseHex4(unsigned int &cp)
	{
		if (m_pos + 4 > m_text.size()) {
			return fail("truncated \\u escape");
		}
		cp = 0;
		for (int i = 0; i < 4; i++) {
			char c = m_text[m_pos++];
			cp <<= 4;
			if (c >= '0' && c <= '9') cp |= c - '0';
			else if (c >= 'a' && c <= 'f') cp |= c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') cp |= c - 'A' + 10;
			else return fail("bad \\u escape");
		}
		return true;
	}

	bool parseString(std::string &out)
	{
		// Caller has consumed the opening quote.
		while (m_pos < m_text.size()) {
			char c = m_text[m_pos++];
			if (c == '"') {
				return true;
			}
			if (c != '\\') {
				out += c;
				continue;
			}
			if (m_pos >= m_text.size()) {
				break;
			}
			char e = m_text[m_pos++];
			switch (e) {
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'u': {
					unsigned int cp;
					if (!parseHex4(cp)) return false;
					if (cp >= 0xD800 && cp <= 0xDBFF && m_pos + 6 <= m_text.size() && m_text[m_pos] == '\\' && m_text[m_pos + 1] == 'u') {
						m_pos += 2;
						unsigned int lo;
						if (!parseHex4(lo)) return false;
						cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
					}
					appendUtf8(out, cp);
					break;
				}
				default:
					return fail("bad escape sequence");
			}
		}
		return fail("unterminated string");
	}

	bool parseNumber(McpJson &out)
	{
		const char *start = m_text.c_str() + m_pos;
		char *end = nullptr;
		double value = strtod(start, &end);
		if (end == start) {
			return fail("bad number");
		}
		m_pos += end - start;
		out = McpJson(value);
		return true;
	}

	bool parseValue(McpJson &out, int depth)
	{
		if (depth > MAX_DEPTH) {
			return fail("nesting too deep");
		}
		skipSpace();
		if (m_pos >= m_text.size()) {
			return fail("unexpected end of input");
		}
		char c = m_text[m_pos];
		if (c == '{') {
			m_pos++;
			out = McpJson::makeObject();
			skipSpace();
			if (m_pos < m_text.size() && m_text[m_pos] == '}') {
				m_pos++;
				return true;
			}
			for (;;) {
				skipSpace();
				if (m_pos >= m_text.size() || m_text[m_pos] != '"') {
					return fail("expected object key");
				}
				m_pos++;
				std::string key;
				if (!parseString(key)) return false;
				skipSpace();
				if (m_pos >= m_text.size() || m_text[m_pos] != ':') {
					return fail("expected ':'");
				}
				m_pos++;
				McpJson value;
				if (!parseValue(value, depth + 1)) return false;
				out.set(key.c_str(), value);
				skipSpace();
				if (m_pos < m_text.size() && m_text[m_pos] == ',') {
					m_pos++;
					continue;
				}
				if (m_pos < m_text.size() && m_text[m_pos] == '}') {
					m_pos++;
					return true;
				}
				return fail("expected ',' or '}'");
			}
		}
		if (c == '[') {
			m_pos++;
			out = McpJson::makeArray();
			skipSpace();
			if (m_pos < m_text.size() && m_text[m_pos] == ']') {
				m_pos++;
				return true;
			}
			for (;;) {
				McpJson value;
				if (!parseValue(value, depth + 1)) return false;
				out.push(value);
				skipSpace();
				if (m_pos < m_text.size() && m_text[m_pos] == ',') {
					m_pos++;
					continue;
				}
				if (m_pos < m_text.size() && m_text[m_pos] == ']') {
					m_pos++;
					return true;
				}
				return fail("expected ',' or ']'");
			}
		}
		if (c == '"') {
			m_pos++;
			std::string s;
			if (!parseString(s)) return false;
			out = McpJson(s);
			return true;
		}
		if (match("true")) { out = McpJson(true); return true; }
		if (match("false")) { out = McpJson(false); return true; }
		if (match("null")) { out = McpJson(); return true; }
		if (c == '-' || (c >= '0' && c <= '9')) {
			return parseNumber(out);
		}
		return fail("unexpected character");
	}
};

} // namespace

bool McpJson::parse(const std::string &text, McpJson &out, std::string &error)
{
	JsonParser parser(text);
	return parser.parseDocument(out, error);
}
