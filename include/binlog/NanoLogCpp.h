/* Copyright (c) 2018 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#pragma once

#include <cstdint>
#include <cstring>

#include <algorithm>
#include <iostream>
#include <utility>
#include <vector>
 /***
  * This file contains all the C++17 constexpr/templated magic that makes
  * the non-preprocessor version of NanoLog work.
  *
  * In essence, it provides 3 types of functions
  *      (1) constexpr functions to analyze the static format string and
  *          produce lookup data structures at compile-time
  *      (2) size/store functions to ascertain the size of the raw arguments
  *          and store them into a char* buffer without compression
  *      (3) compress functions to take the raw arguments from the buffers
  *          and produce a more compact encoding that's compatible with the
  *          NanoLog decompressor.
  */
namespace NanoLogInternal {

	enum ParamType : int32_t {
		// Indicates that there is a problem with the parameter
		INVALID = -6,

		// Indicates a dynamic width (i.e. the '*' in  %*.d)
		DYNAMIC_WIDTH = -5,

		// Indicates dynamic precision (i.e. the '*' in %.*d)
		DYNAMIC_PRECISION = -4,

		// Indicates that the parameter is not a string type (i.e. %d, %lf)
		NON_STRING = -3,

		// Indicates the parameter is a string and has a dynamic precision
		// (i.e. '%.*s' )
		STRING_WITH_DYNAMIC_PRECISION = -2,

		// Indicates a string with no precision specified (i.e. '%s' )
		STRING_WITH_NO_PRECISION = -1,

		// All non-negative values indicate a string with a precision equal to its
		// enum value casted as an int32_t
		STRING = 0
	};


	/**
	 * Analyzes a static printf style format string and extracts type information
	 * about the p-th parameter that would be used in a corresponding NANO_LOG()
	 * invocation.
	 *
	 * \tparam N
	 *      Length of the static format string (automatically deduced)
	 * \param fmt
	 *      Format string to parse
	 * \param paramNum
	 *      p-th parameter to return type information for (starts from zero)
	 * \return
	 *      Returns an ParamType enum describing the type of the parameter
	 */
	std::vector<ParamType> getParamInfo(const char* fmt, int N);


} /* Namespace NanoLogInternal */

