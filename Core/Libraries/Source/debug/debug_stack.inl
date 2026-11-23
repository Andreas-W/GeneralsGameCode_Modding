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

// Used for dynamically linking to dbghelp.dll functions.

// keep this always as first entry
DBGHELP(SymInitialize,
        BOOL,
        (HANDLE hProcess, PCSTR UserSearchPath, BOOL fInvadeProcess))

DBGHELP(SymGetOptions,
        DWORD,
        ())

DBGHELP(SymSetOptions,
        DWORD,
        (DWORD SymOptions))

DBGHELP_ADDR(StackWalk,
        BOOL,
        (DWORD MachineType, HANDLE hProcess, HANDLE hThread, LPSTACKFRAME StackFrame,
        LPVOID ContextRecord, PREAD_PROCESS_MEMORY_ROUTINE ReadMemoryRoutine,
        PFUNCTION_TABLE_ACCESS_ROUTINE FunctionTableAccessRoutine,
        PGET_MODULE_BASE_ROUTINE GetModuleBaseRoutine,
        PTRANSLATE_ADDRESS_ROUTINE TranslateAddress))

DBGHELP_ADDR(SymFunctionTableAccess,
        LPVOID,
        (HANDLE hProcess, DBGHELP_ADDR_TYPE AddrBase))

DBGHELP_ADDR(SymGetModuleBase,
        DBGHELP_ADDR_TYPE,
        (HANDLE hProcess, DBGHELP_ADDR_TYPE dwAddr))

DBGHELP_ADDR(SymGetSymFromAddr,
        BOOL,
        (HANDLE hProcess, DBGHELP_ADDR_TYPE Address, DBGHELP_ADDR_TYPE *Displacement,
        PIMAGEHLP_SYMBOL Symbol))

DBGHELP_ADDR(SymGetLineFromAddr,
        BOOL,
        (HANDLE hProcess, DBGHELP_ADDR_TYPE dwAddr, PDWORD pdwDisplacement,
        PIMAGEHLP_LINE Line))

// keep this always as last entry
DBGHELP(SymCleanup,
        BOOL,
        (HANDLE hProcess))
