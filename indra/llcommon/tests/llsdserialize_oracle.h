/**
 * @file llsdserialize_oracle.h
 * @brief The viewer's C++ LLSD parsers and formatters as they were before
 * vkstorm-rust rung two, kept as the behavioural oracle of the parity test
 * and the benchmark. Derived mechanically from llsdserialize.h at the commit
 * that replaced them; not used by the viewer.
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#ifndef LL_LLSDSERIALIZE_ORACLE_H
#define LL_LLSDSERIALIZE_ORACLE_H

#include <iosfwd>
#include "llpointer.h"
#include "llrefcount.h"
#include "llsd.h"
#include "llsdserialize.h"

namespace llsd_oracle
{

/**
 * @class OracleParser
 * @brief Abstract base class for LLSD parsers.
 */
class OracleParser : public LLRefCount
{
protected:
    /**
     * @brief Destructor
     */
    virtual ~OracleParser();

public:
    /**
     * @brief Anonymous enum to indicate parsing failure.
     */
    enum
    {
        PARSE_FAILURE = -1
    };

    /**
     * @brief Constructor
     */
    OracleParser();

    /**
     * @brief Call this method to parse a stream for LLSD.
     *
     * This method parses the istream for a structured data. This
     * method assumes that the istream is a complete llsd object --
     * for example an opened and closed map with an arbitrary nesting
     * of elements. This method will return after reading one data
     * object, allowing continued reading from the stream by the
     * caller.
     * @param istr The input stream.
     * @param data[out] The newly parse structured data.
     * @param max_bytes The maximum number of bytes that will be in
     * the stream. Pass in LLSDSerialize::SIZE_UNLIMITED (-1) to set no
     * byte limit.
     * @return Returns the number of LLSD objects parsed into
     * data. Returns PARSE_FAILURE (-1) on parse failure.
     */
    S32 parse(std::istream& istr, LLSD& data, llssize max_bytes, S32 max_depth = -1);

    /** Like parse(), but uses a different call (istream.getline()) to read by lines
     *  This API is better suited for XML, where the parse cannot tell
     *  where the document actually ends.
     */
    S32 parseLines(std::istream& istr, LLSD& data);

    /**
     * @brief Resets the parser so parse() or parseLines() can be called again for another <llsd> chunk.
     */
    void reset()    { doReset();    };


protected:
    /**
     * @brief Pure virtual base for doing the parse.
     *
     * This method parses the istream for a structured data. This
     * method assumes that the istream is a complete llsd object --
     * for example an opened and closed map with an arbitrary nesting
     * of elements. This method will return after reading one data
     * object, allowing continued reading from the stream by the
     * caller.
     * @param istr The input stream.
     * @param data[out] The newly parse structured data.
     * @param max_depth Max depth parser will check before exiting
     *  with parse error, -1 - unlimited.
     * @return Returns the number of LLSD objects parsed into
     * data. Returns PARSE_FAILURE (-1) on parse failure.
     */
    virtual S32 doParse(std::istream& istr, LLSD& data, S32 max_depth = -1) const = 0;

    /**
     * @brief Virtual default function for resetting the parser
     */
    virtual void doReset()  {};

    /* @name Simple istream helper methods
     *
     * These helper methods exist to help correctly use the
     * mMaxBytesLeft without really thinking about it for most simple
     * operations. Use of the streamtools in llstreamtools.h will
     * require custom wrapping.
     */
    //@{
    /**
     * @brief get a byte off the stream
     *
     * @param istr The istream to work with.
     * @return returns the next character.
     */
    int get(std::istream& istr) const;

    /**
     * @brief get several bytes off the stream into a buffer.
     *
     * @param istr The istream to work with.
     * @param s The buffer to get into
     * @param n Extract maximum of n-1 bytes and null temrinate.
     * @param delim Delimiter to get until found.
     * @return Returns istr.
     */
    std::istream& get(
        std::istream& istr,
        char* s,
        std::streamsize n,
        char delim) const;

    /**
     * @brief get several bytes off the stream into a streambuf
     *
     * @param istr The istream to work with.
     * @param sb The streambuf to read into
     * @param delim Delimiter to get until found.
     * @return Returns istr.
     */
    std::istream& get(
        std::istream& istr,
        std::streambuf& sb,
        char delim) const;

    /**
     * @brief ignore the next byte on the istream
     *
     * @param istr The istream to work with.
     * @return Returns istr.
     */
    std::istream& ignore(std::istream& istr) const;

    /**
     * @brief put the last character retrieved back on the stream
     *
     * @param istr The istream to work with.
     * @param c The character to put back
     * @return Returns istr.
     */
    std::istream& putback(std::istream& istr, char c) const;

    /**
     * @brief read a block of n characters into a buffer
     *
     * @param istr The istream to work with.
     * @param s The buffer to read into
     * @param n The number of bytes to read.
     * @return Returns istr.
     */
    std::istream& read(std::istream& istr, char* s, std::streamsize n) const;
    //@}

protected:
    /**
     * @brief Accunt for bytes read outside of the istream helpers.
     *
     * Conceptually const since it only modifies mutable members.
     * @param bytes The number of bytes read.
     */
    void account(llssize bytes) const;

protected:
    /**
     * @brief boolean to set if byte counts should be checked during parsing.
     */
    bool mCheckLimits;

    /**
     * @brief The maximum number of bytes left to be parsed.
     */
    mutable llssize mMaxBytesLeft;

    /**
     * @brief Use line-based reading to get text
     */
    bool mParseLines;
};

/**
 * @class OracleNotationParser
 * @brief Parser which handles the original notation format for LLSD.
 */
class OracleNotationParser : public OracleParser
{
protected:
    /**
     * @brief Destructor
     */
    virtual ~OracleNotationParser();

public:
    /**
     * @brief Constructor
     */
    OracleNotationParser();

protected:
    /**
     * @brief Call this method to parse a stream for LLSD.
     *
     * This method parses the istream for a structured data. This
     * method assumes that the istream is a complete llsd object --
     * for example an opened and closed map with an arbitrary nesting
     * of elements. This method will return after reading one data
     * object, allowing continued reading from the stream by the
     * caller.
     * @param istr The input stream.
     * @param data[out] The newly parse structured data. Undefined on failure.
     * @param max_depth Max depth parser will check before exiting
     *  with parse error, -1 - unlimited.
     * @return Returns the number of LLSD objects parsed into
     * data. Returns PARSE_FAILURE (-1) on parse failure.
     */
    virtual S32 doParse(std::istream& istr, LLSD& data, S32 max_depth = -1) const;

private:
    /**
     * @brief Parse a map from the istream
     *
     * @param istr The input stream.
     * @param map The map to add the parsed data.
     * @param max_depth Allowed parsing depth.
     * @return Returns The number of LLSD objects parsed into data.
     */
    S32 parseMap(std::istream& istr, LLSD& map, S32 max_depth) const;

    /**
     * @brief Parse an array from the istream.
     *
     * @param istr The input stream.
     * @param array The array to append the parsed data.
     * @param max_depth Allowed parsing depth.
     * @return Returns The number of LLSD objects parsed into data.
     */
    S32 parseArray(std::istream& istr, LLSD& array, S32 max_depth) const;

    /**
     * @brief Parse a string from the istream and assign it to data.
     *
     * @param istr The input stream.
     * @param data[out] The data to assign.
     * @return Retuns true if a complete string was parsed.
     */
    bool parseString(std::istream& istr, LLSD& data) const;

    /**
     * @brief Parse binary data from the stream.
     *
     * @param istr The input stream.
     * @param data[out] The data to assign.
     * @return Retuns true if a complete blob was parsed.
     */
    bool parseBinary(std::istream& istr, LLSD& data) const;
};

/**
 * @class OracleXMLParser
 * @brief Parser which handles XML format LLSD.
 */
class OracleXMLParser : public OracleParser
{
protected:
    /**
     * @brief Destructor
     */
    virtual ~OracleXMLParser();

public:
    /**
     * @brief Constructor
     */
    OracleXMLParser(bool emit_errors=true);

protected:
    /**
     * @brief Call this method to parse a stream for LLSD.
     *
     * This method parses the istream for a structured data. This
     * method assumes that the istream is a complete llsd object --
     * for example an opened and closed map with an arbitrary nesting
     * of elements. This method will return after reading one data
     * object, allowing continued reading from the stream by the
     * caller.
     * @param istr The input stream.
     * @param data[out] The newly parse structured data.
     * @param max_depth Max depth parser will check before exiting
     *  with parse error, -1 - unlimited.
     * @return Returns the number of LLSD objects parsed into
     * data. Returns PARSE_FAILURE (-1) on parse failure.
     */
    virtual S32 doParse(std::istream& istr, LLSD& data, S32 max_depth = -1) const;

    /**
     * @brief Virtual default function for resetting the parser
     */
    virtual void doReset();

public:
    void parsePart(const char* buf, llssize len);

private:
    class Impl;
    Impl& impl;
};

/**
 * @class OracleBinaryParser
 * @brief Parser which handles binary formatted LLSD.
 */
class OracleBinaryParser : public OracleParser
{
protected:
    /**
     * @brief Destructor
     */
    virtual ~OracleBinaryParser();

public:
    /**
     * @brief Constructor
     */
    OracleBinaryParser();

protected:
    /**
     * @brief Call this method to parse a stream for LLSD.
     *
     * This method parses the istream for a structured data. This
     * method assumes that the istream is a complete llsd object --
     * for example an opened and closed map with an arbitrary nesting
     * of elements. This method will return after reading one data
     * object, allowing continued reading from the stream by the
     * caller.
     * @param istr The input stream.
     * @param data[out] The newly parse structured data.
     * @param max_depth Max depth parser will check before exiting
     *  with parse error, -1 - unlimited.
     * @return Returns the number of LLSD objects parsed into
     * data. Returns -1 on parse failure.
     */
    virtual S32 doParse(std::istream& istr, LLSD& data, S32 max_depth = -1) const;

private:
    /**
     * @brief Parse a map from the istream
     *
     * @param istr The input stream.
     * @param map The map to add the parsed data.
     * @param max_depth Allowed parsing depth.
     * @return Returns The number of LLSD objects parsed into data.
     */
    S32 parseMap(std::istream& istr, LLSD& map, S32 max_depth) const;

    /**
     * @brief Parse an array from the istream.
     *
     * @param istr The input stream.
     * @param array The array to append the parsed data.
     * @param max_depth Allowed parsing depth.
     * @return Returns The number of LLSD objects parsed into data.
     */
    S32 parseArray(std::istream& istr, LLSD& array, S32 max_depth) const;

    /**
     * @brief Parse a string from the istream and assign it to data.
     *
     * @param istr The input stream.
     * @param value[out] The string to assign.
     * @return Retuns true if a complete string was parsed.
     */
    bool parseString(std::istream& istr, std::string& value) const;
};


/**
 * @class OracleFormatter
 * @brief Abstract base class for formatting LLSD.
 */
class OracleFormatter : public LLRefCount
{
protected:
    /**
     * @brief Destructor
     */
    virtual ~OracleFormatter();

public:
    /**
     * Options for output
     */
    typedef enum e_formatter_options_type
    {
        OPTIONS_NONE = 0,
        OPTIONS_PRETTY = 1,
        OPTIONS_PRETTY_BINARY = 2
    } EFormatterOptions;

    /**
     * @brief Constructor
     */
    OracleFormatter(bool boolAlpha=false, const std::string& realFormat="",
                  EFormatterOptions options=OPTIONS_PRETTY_BINARY);

    /**
     * @brief Set the boolean serialization format.
     *
     * @param alpha Serializes boolean as alpha if true.
     */
    void boolalpha(bool alpha);

    /**
     * @brief Set the real format
     *
     * By default, the formatter will use default double serialization
     * which is frequently frustrating for many applications. You can
     * set the precision on the stream independently, but that still
     * might not work depending on the value.
     * EXAMPLES:<br>
     * %.2f<br>
     * @param format A format string which follows the printf format
     * rules. Specify an empty string to return to default formatting.
     */
    void realFormat(const std::string& format);

    /**
     * @brief Call this method to format an LLSD to a stream with options as
     * set by the constructor.
     *
     * @param data The data to write.
     * @param ostr The destination stream for the data.
     * @return Returns The number of LLSD objects formatted out
     */
    S32 format(const LLSD& data, std::ostream& ostr) const;

    /**
     * @brief Call this method to format an LLSD to a stream, passing options
     * explicitly.
     *
     * @param data The data to write.
     * @param ostr The destination stream for the data.
     * @param options OPTIONS_NONE to emit LLSD::Binary as raw bytes
     * @return Returns The number of LLSD objects formatted out
     */
    virtual S32 format(const LLSD& data, std::ostream& ostr, EFormatterOptions options) const;

protected:
    /**
     * @brief Implementation to format the data. This is called recursively.
     *
     * @param data The data to write.
     * @param ostr The destination stream for the data.
     * @return Returns The number of LLSD objects formatted out
     */
    virtual S32 format_impl(const LLSD& data, std::ostream& ostr, EFormatterOptions options,
                            U32 level) const = 0;

    /**
     * @brief Helper method which appropriately obeys the real format.
     *
     * @param real The real value to format.
     * @param ostr The destination stream for the data.
     */
    void formatReal(LLSD::Real real, std::ostream& ostr) const;

    bool mBoolAlpha;
    std::string mRealFormat;
    EFormatterOptions mOptions;
};


/**
 * @class OracleNotationFormatter
 * @brief Formatter which outputs the original notation format for LLSD.
 */
class OracleNotationFormatter : public OracleFormatter
{
protected:
    /**
     * @brief Destructor
     */
    virtual ~OracleNotationFormatter();

public:
    /**
     * @brief Constructor
     */
    OracleNotationFormatter(bool boolAlpha=false, const std::string& realFormat="",
                          EFormatterOptions options=OPTIONS_PRETTY_BINARY);

    /**
     * @brief Helper static method to return a notation escaped string
     *
     * This method will return the notation escaped string, but not
     * the surrounding serialization identifiers such as a double or
     * single quote. It will be up to the caller to embed those as
     * appropriate.
     * @param in The raw, unescaped string.
     * @return Returns an escaped string appropriate for serialization.
     */
    static std::string escapeString(const std::string& in);

protected:
    /**
     * @brief Implementation to format the data. This is called recursively.
     *
     * @param data The data to write.
     * @param ostr The destination stream for the data.
     * @return Returns The number of LLSD objects formatted out
     */
    S32 format_impl(const LLSD& data, std::ostream& ostr, EFormatterOptions options,
                    U32 level) const override;
};


/**
 * @class OracleXMLFormatter
 * @brief Formatter which outputs the LLSD as XML.
 */
class OracleXMLFormatter : public OracleFormatter
{
protected:
    /**
     * @brief Destructor
     */
    virtual ~OracleXMLFormatter();

public:
    /**
     * @brief Constructor
     */
    OracleXMLFormatter(bool boolAlpha=false, const std::string& realFormat="",
                     EFormatterOptions options=OPTIONS_PRETTY_BINARY);

    /**
     * @brief Helper static method to return an xml escaped string
     *
     * @param in A valid UTF-8 string.
     * @return Returns an escaped string appropriate for serialization.
     */
    static std::string escapeString(const std::string& in);

    /**
     * @brief Call this method to format an LLSD to a stream.
     *
     * @param data The data to write.
     * @param ostr The destination stream for the data.
     * @return Returns The number of LLSD objects formatted out
     */
    S32 format(const LLSD& data, std::ostream& ostr, EFormatterOptions options) const override;

    // also pull down base-class format() method that isn't overridden
    using OracleFormatter::format;

protected:
    /**
     * @brief Implementation to format the data. This is called recursively.
     *
     * @param data The data to write.
     * @param ostr The destination stream for the data.
     * @return Returns The number of LLSD objects formatted out
     */
    S32 format_impl(const LLSD& data, std::ostream& ostr, EFormatterOptions options,
                    U32 level) const override;
};


/**
 * @class OracleBinaryFormatter
 * @brief Formatter which outputs the LLSD as a binary notation format.
 *
 * The binary format is a compact and efficient representation of
 * structured data useful for when transmitting over a small data pipe
 * or when transmission frequency is very high.<br>
 *
 * The normal boolalpha and real format commands are ignored.<br>
 *
 * All integers are transmitted in network byte order. The format is:<br>
 * Undefined: '!'<br>
 * Boolean: character '1' for true character '0' for false<br>
 * Integer: 'i' + 4 bytes network byte order<br>
 * Real: 'r' + 8 bytes IEEE double<br>
 * UUID: 'u' + 16 byte unsigned integer<br>
 * String: 's' + 4 byte integer size + string<br>
 * Date: 'd' + 8 byte IEEE double for seconds since epoch<br>
 * URI: 'l' + 4 byte integer size + string uri<br>
 * Binary: 'b' + 4 byte integer size + binary data<br>
 * Array: '[' + 4 byte integer size  + all values + ']'<br>
 * Map: '{' + 4 byte integer size  every(key + value) + '}'<br>
 *  map keys are serialized as 'k' + 4 byte integer size + string
 */
class OracleBinaryFormatter : public OracleFormatter
{
protected:
    /**
     * @brief Destructor
     */
    virtual ~OracleBinaryFormatter();

public:
    /**
     * @brief Constructor
     */
    OracleBinaryFormatter(bool boolAlpha=false, const std::string& realFormat="",
                        EFormatterOptions options=OPTIONS_PRETTY_BINARY);

protected:
    /**
     * @brief Implementation to format the data. This is called recursively.
     *
     * @param data The data to write.
     * @param ostr The destination stream for the data.
     * @return Returns The number of LLSD objects formatted out
     */
    S32 format_impl(const LLSD& data, std::ostream& ostr, EFormatterOptions options,
                    U32 level) const override;

    /**
     * @brief Helper method to serialize strings
     *
     * This method serializes a network byte order size and the raw
     * string contents.
     * @param string The string to write.
     * @param ostr The destination stream for the data.
     */
    void formatString(const std::string& string, std::ostream& ostr) const;
};



} // namespace llsd_oracle

#endif // LL_LLSDSERIALIZE_ORACLE_H
