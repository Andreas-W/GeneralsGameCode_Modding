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

// McpJson.h
// Minimal JSON value used by the WorldBuilder MCP command bridge.

#pragma once

#include <string>
#include <vector>

class McpJson
{
public:
	enum Type
	{
		TYPE_NULL,
		TYPE_BOOL,
		TYPE_NUMBER,
		TYPE_STRING,
		TYPE_ARRAY,
		TYPE_OBJECT
	};

	McpJson() : m_type(TYPE_NULL), m_bool(false), m_number(0) {}
	McpJson(bool b) : m_type(TYPE_BOOL), m_bool(b), m_number(0) {}
	McpJson(int n) : m_type(TYPE_NUMBER), m_bool(false), m_number(n) {}
	McpJson(unsigned int n) : m_type(TYPE_NUMBER), m_bool(false), m_number(n) {}
	McpJson(float n) : m_type(TYPE_NUMBER), m_bool(false), m_number(n) {}
	McpJson(double n) : m_type(TYPE_NUMBER), m_bool(false), m_number(n) {}
	McpJson(const char *s) : m_type(TYPE_STRING), m_bool(false), m_number(0), m_string(s ? s : "") {}
	McpJson(const std::string &s) : m_type(TYPE_STRING), m_bool(false), m_number(0), m_string(s) {}

	static McpJson makeArray() { McpJson j; j.m_type = TYPE_ARRAY; return j; }
	static McpJson makeObject() { McpJson j; j.m_type = TYPE_OBJECT; return j; }

	Type getType() const { return m_type; }
	bool isNull() const { return m_type == TYPE_NULL; }
	bool isBool() const { return m_type == TYPE_BOOL; }
	bool isNumber() const { return m_type == TYPE_NUMBER; }
	bool isString() const { return m_type == TYPE_STRING; }
	bool isArray() const { return m_type == TYPE_ARRAY; }
	bool isObject() const { return m_type == TYPE_OBJECT; }

	bool asBool() const { return m_bool; }
	double asNumber() const { return m_number; }
	const std::string &asString() const { return m_string; }

	// Array access. Out of range reads return a null value.
	size_t size() const;
	const McpJson &at(size_t ndx) const;
	McpJson &push(const McpJson &value);

	// Object access. Missing keys read as a null value.
	bool has(const char *key) const;
	const McpJson &get(const char *key) const;
	McpJson &set(const char *key, const McpJson &value);	///< Returns *this so calls can be chained.
	size_t numKeys() const { return m_keys.size(); }
	const std::string &keyAt(size_t ndx) const { return m_keys[ndx]; }
	const McpJson &valueAt(size_t ndx) const { return m_values[ndx]; }

	std::string dump() const;
	static bool parse(const std::string &text, McpJson &out, std::string &error);

private:
	void dumpTo(std::string &out) const;

	Type m_type;
	bool m_bool;
	double m_number;
	std::string m_string;
	std::vector<std::string> m_keys;		///< Object keys, parallel to m_values.
	std::vector<McpJson> m_values;		///< Array elements or object values.
};
