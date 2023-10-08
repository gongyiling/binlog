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
#include "binlog/NanoLogCpp.h"
#include <cstdint>
#include <cstring>

#include <algorithm>
#include <iostream>
#include <utility>


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

	/**
	 * Checks whether a character is with the terminal set of format specifier
	 * characters according to the printf specification:
	 * http://www.cplusplus.com/reference/cstdio/printf/
	 *
	 * \param c
	 *      character to check
	 * \return
	 *      true if the character is in the set, indicating the end of the specifier
	 */
	inline bool isTerminal(char c)
	{
		return c == 'd' || c == 'i'
			|| c == 'u' || c == 'o'
			|| c == 'x' || c == 'X'
			|| c == 'f' || c == 'F'
			|| c == 'e' || c == 'E'
			|| c == 'g' || c == 'G'
			|| c == 'a' || c == 'A'
			|| c == 'c' || c == 'p'
			|| c == '%' || c == 's'
			|| c == 'n';
	}

	/**
	 * Checks whether a character is in the set of characters that specifies
	 * a flag according to the printf specification:
	 * http://www.cplusplus.com/reference/cstdio/printf/
	 *
	 * \param c
	 *      character to check
	 * \return
	 *      true if the character is in the set
	 */
	inline bool isFlag(char c)
	{
		return c == '-' || c == '+' || c == ' ' || c == '#' || c == '0';
	}

	/**
	 * Checks whether a character is in the set of characters that specifies
	 * a length field according to the printf specification:
	 * http://www.cplusplus.com/reference/cstdio/printf/
	 *
	 * \param c
	 *      character to check
	 * \return
	 *      true if the character is in the set
	 */
	inline bool isLength(char c)
	{
		return c == 'h' || c == 'l' || c == 'j'
			|| c == 'z' || c == 't' || c == 'L';
	}

	/**
	 * Checks whether a character is a digit (0-9) or not.
	 *
	 * \param c
	 *      character to check
	 * \return
	 *      true if the character is a digit
	 */
	inline bool isDigit(char c) {
		return (c >= '0' && c <= '9');
	}

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
	std::vector<ParamType> getParamInfo(const char* fmt, int N)
	{
		std::vector<ParamType> Params;
		int pos = 0;
		while (pos < N - 1) {

			// The code below searches for something that looks like a printf
			// specifier (i.e. something that follows the format of
			// %<flags><width>.<precision><length><terminal>). We only care
			// about precision and type, so everything else is ignored.
			if (fmt[pos] != '%') {
				++pos;
				continue;
			}
			else {
				// Note: gcc++ 5,6,7,8 seems to hang whenever one uses the construct
				// "if (...) {... continue; }" without an else in constexpr
				// functions. Hence, we have the code here wrapped in an else {...}
				// I reported this bug to the developers here
				// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=86767
				++pos;

				// Two %'s in a row => Comment
				if (fmt[pos] == '%') {
					++pos;
					continue;
				}
				else {

					// Consume flags
					while (NanoLogInternal::isFlag(fmt[pos]))
						++pos;

					// Consume width
					if (fmt[pos] == '*') {
						Params.push_back(ParamType::DYNAMIC_WIDTH);
						++pos;
					}
					else {
						while (NanoLogInternal::isDigit(fmt[pos]))
							++pos;
					}

					// Consume precision
					bool hasDynamicPrecision = false;
					int precision = -1;
					if (fmt[pos] == '.') {
						++pos;  // consume '.'

						if (fmt[pos] == '*') {
							Params.push_back(ParamType::DYNAMIC_PRECISION);
							hasDynamicPrecision = true;
							++pos;
						}
						else {
							precision = 0;
							while (NanoLogInternal::isDigit(fmt[pos])) {
								precision = 10 * precision + (fmt[pos] - '0');
								++pos;
							}
						}
					}

					// consume length
					while (isLength(fmt[pos]))
						++pos;

					// Consume terminal
					if (!NanoLogInternal::isTerminal(fmt[pos])) {
						Params.push_back(ParamType::INVALID);
						//throw std::invalid_argument(
						//        "Unrecognized format specifier after %");
					}

					// Fail on %n specifiers (i.e. store position to address) since
					// we cannot know the position without formatting.
					if (fmt[pos] == 'n') {
						Params.push_back(ParamType::INVALID);
						/* throw std::invalid_argument(
								 "%n specifiers are not support in NanoLog!");*/
					}

					if (fmt[pos] != 's')
					{
						Params.push_back(ParamType::NON_STRING);
					}
					else if (hasDynamicPrecision)
					{
						Params.push_back(ParamType::STRING_WITH_DYNAMIC_PRECISION);
					}
					else if (precision == -1)
					{
						Params.push_back(ParamType::STRING_WITH_NO_PRECISION);
					}
					else
					{
						Params.push_back(ParamType(precision));
					}
					++pos;
				}
			}
		}

		return Params;
	}

	/**
	 * For a single non-string, non-void pointer argument, return the number
	 * of bytes needed to represent the full-width type without compression.
	 *
	 * \tparam T
	 *      Actual type of the argument (automatically deduced)
	 *
	 * \param fmtType
	 *      Type of the argument according to the original printf-like format
	 *      string (needed to disambiguate 'const char*' types from being
	 *      '%p' or '%s' and for precision info)
	 * \param[in/out] previousPrecision
	 *      Store the last 'precision' format specifier type encountered
	 *      (as dictated by the fmtType)
	 * \param stringSize
	 *      Byte length of the current argument, if it is a string, else, undefined
	 * \param arg
	 *      Argument to compute the size for
	 *
	 * \return
	 *      Size of the full-width argument without compression
	 */
	template<typename T>
	inline size_t getArgSize(const ParamType fmtType,
		uint64_t& previousPrecision,
		size_t& stringSize,
		T arg)
	{
		if (fmtType == ParamType::DYNAMIC_PRECISION)
			previousPrecision = as_uint64_t(arg);

		return sizeof(T);
	}

	/**
	 * "void *" specialization for getArgSize. (See documentation above).
	 */
	inline size_t
		getArgSize(const ParamType,
			uint64_t& previousPrecision,
			size_t& stringSize,
			const void*)
	{
		return sizeof(void*);
	}

	/**
	 * String specialization for getArgSize. Returns the number of bytes needed
	 * to represent a string (with consideration for any 'precision' specifiers
	 * in the original format string and) without a NULL terminator and with a
	 * uint32_t length.
	 *
	 * \param fmtType
	 *      Type of the argument according to the original printf-like format
	 *      string (needed to disambiguate 'const char*' types from being
	 *      '%p' or '%s' and for precision info)
	 * \param previousPrecision
	 *      Store the last 'precision' format specifier type encountered
	 *      (as dictated by the fmtType)
	 * \param stringBytes
	 *      Byte length of the current argument, if it is a string, else, undefined
	 * \param str
	 *      String to compute the length for
	 * \return
	 *      Length of the string str with a uint32_t length and no NULL terminator
	 */
	inline size_t
		getArgSize(const ParamType fmtType,
			uint64_t& previousPrecision,
			size_t& stringBytes,
			const char* str)
	{
		if (fmtType <= ParamType::NON_STRING)
			return sizeof(void*);

		stringBytes = strlen(str);
		uint32_t fmtLength = static_cast<uint32_t>(fmtType);

		// Strings with static length specifiers (ex %.10s), have non-negative
		// ParamTypes equal to the static length. Thus, we use that value to
		// truncate the string as necessary.
		if (fmtType >= ParamType::STRING && stringBytes > fmtLength)
			stringBytes = fmtLength;

		// If the string had a dynamic precision specified (i.e. %.*s), use
		// the previous parameter as the precision and truncate as necessary.
		else if (fmtType == ParamType::STRING_WITH_DYNAMIC_PRECISION &&
			stringBytes > previousPrecision)
			stringBytes = previousPrecision;

		return stringBytes + sizeof(uint32_t);
	}

	/**
	 * Wide-character string specialization of the above.
	 */
	inline size_t
		getArgSize(const ParamType fmtType,
			uint64_t& previousPrecision,
			size_t& stringBytes,
			const wchar_t* wstr)
	{
		if (fmtType <= ParamType::NON_STRING)
			return sizeof(void*);

		stringBytes = wcslen(wstr);
		uint32_t fmtLength = static_cast<uint32_t>(fmtType);

		// Strings with static length specifiers (ex %.10s), have non-negative
		// ParamTypes equal to the static length. Thus, we use that value to
		// truncate the string as necessary.
		if (fmtType >= ParamType::STRING && stringBytes > fmtLength)
			stringBytes = fmtLength;

		// If the string had a dynamic precision specified (i.e. %.*s), use
		// the previous parameter as the precision and truncate as necessary.
		else if (fmtType == ParamType::STRING_WITH_DYNAMIC_PRECISION &&
			stringBytes > previousPrecision)
			stringBytes = previousPrecision;

		stringBytes *= sizeof(wchar_t);
		return stringBytes + sizeof(uint32_t);
	}

} /* Namespace NanoLogInternal */

