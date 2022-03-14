// -----------------------------------------------------------------------------------------------------------
// Copyright (c) 2006-2021, Knut Reinert & Freie Universität Berlin
// Copyright (c) 2016-2021, Knut Reinert & MPI für molekulare Genetik
// This file may be used, modified and/or redistributed under the terms of the 3-clause BSD-License
// shipped with this file and also available at: https://github.com/seqan/sharg-parser/blob/master/LICENSE.md
// -----------------------------------------------------------------------------------------------------------

/*!\file
 * \author Svenja Mehringer <svenja.mehringer AT fu-berlin.de>
 * \brief Provides some standard validators for (positional) options.
 */

#pragma once

#include <concepts>
#include <fstream>
#include <ranges>
#include <regex>

#include <sharg/detail/safe_filesystem_entry.hpp>
#include <sharg/detail/to_string.hpp>
#include <sharg/exceptions.hpp>

namespace sharg
{

/*!\concept sharg::validator
 * \brief The concept for option validators passed to add_option/positional_option.
 * \ingroup argument_parser
 *
 * \details
 *
 * When adding (positional) options to the sharg::argument_parser you may pass a
 * [function object](https://en.cppreference.com/w/cpp/named_req/FunctionObject) that models
 * sharg::validator which checks the option value provided by the user for some constraint.
 *
 * SeqAn provides several common-use-case validators, e.g. the sharg::arithmetic_range_validator.
 *
 * \include test/snippet/argument_parser/validators_2.cpp
 *
 * You can learn more about SeqAn validators in our tutorial \ref section_validation.
 *
 * To implement your own validator please refer to the detailed concept description below.
 */
template <typename validator_type>
concept validator = std::copyable<std::remove_cvref_t<validator_type>> &&
                    requires(validator_type validator,
                             typename std::remove_reference_t<validator_type>::option_value_type value)
{
    typename std::remove_reference_t<validator_type>::option_value_type;

    {validator(value)} -> std::same_as<void>;
    {validator.get_help_page_message()} -> std::same_as<std::string>;
};

/*!\brief A validator that checks whether a number is inside a given range.
 * \ingroup argument_parser
 * \implements sharg::validator
 * \tparam option_value_t The value type of the range; must model std::is_arithmetic_v.
 *
 * \details
 *
 * On construction, the validator must receive a maximum and a minimum number.
 * The class than acts as a functor, that throws a sharg::validation_error
 * exception whenever a given value does not lie inside the given min/max range.
 *
 * \include test/snippet/argument_parser/validators_1.cpp
 *
 * \remark For a complete overview, take a look at \ref argument_parser
 */
template <typename option_value_t>
//!\cond
    requires std::is_arithmetic_v<option_value_t>
//!\endcond
class arithmetic_range_validator
{
public:
    //!\brief The type of value that this validator invoked upon.
    using option_value_type = option_value_t;

    /*!\brief The constructor.
     * \param[in] min_ Minimum set for the range to test.
     * \param[in] max_ Maximum set for the range to test.
     */
    arithmetic_range_validator(option_value_type const min_, option_value_type const max_) :
        min{min_}, max{max_}, valid_range_str{"[" + std::to_string(min_) + "," + std::to_string(max_) + "]"}
    {}

    /*!\brief Tests whether cmp lies inside [`min`, `max`].
     * \param cmp The input value to check.
     * \throws sharg::validation_error
     */
    void operator()(option_value_type const & cmp) const
    {
        if (!((cmp <= max) && (cmp >= min)))
            throw validation_error{"Value " + std::to_string(cmp) + " is not in range " + valid_range_str + "."};
    }

    /*!\brief Tests whether every element in \p range lies inside [`min`, `max`].
     * \tparam range_type The type of range to check; must model std::ranges::forward_range. The value type must model
     *                    std::is_arithmetic_v.
     * \param  range      The input range to iterate over and check every element.
     * \throws sharg::validation_error
     */
    template <std::ranges::forward_range range_type>
    //!\cond
        requires std::is_arithmetic_v<std::ranges::range_value_t<range_type>>
    //!\endcond
    void operator()(range_type const & range) const
    {
        std::for_each(range.begin(), range.end(), [&] (auto cmp) { (*this)(cmp); });
    }

    //!\brief Returns a message that can be appended to the (positional) options help page info.
    std::string get_help_page_message() const
    {
        return std::string{"Value must be in range "} + valid_range_str + ".";
    }

private:
    //!\brief Minimum of the range to test.
    option_value_type min{};

    //!\brief Maximum of the range to test.
    option_value_type max{};

    //!\brief The range as string
    std::string valid_range_str{};
};

/*!\brief A validator that checks whether a value is inside a list of valid values.
 * \ingroup argument_parser
 * \implements sharg::validator
 * \tparam option_value_t The type the validator is called on. Must model sharg::argument_parser_compatible_option.
 *
 * \details
 *
 * On construction, the validator must receive a range or parameter pack of valid values.
 * The class than acts as a functor, that throws a sharg::validation_error
 * exception whenever a given value is not in the given list.
 *
 * \note In order to simplify the chaining of validators, the option value type is deduced to `std::string` if the
 *       range's value type is convertible to it. Otherwise, the option value type is deduced to the value type of the
 *       range.
 *
 * \include test/snippet/argument_parser/validators_2.cpp
 *
 * \remark For a complete overview, take a look at \ref argument_parser
 */
template <argument_parser_compatible_option option_value_t>
class value_list_validator
{
public:
    //!\brief Type of values that are tested by validator
    using option_value_type = option_value_t;

    /*!\name Constructors, destructor and assignment
     * \{
     */
    value_list_validator() = default;                                         //!< Defaulted.
    value_list_validator(value_list_validator const &) = default;             //!< Defaulted.
    value_list_validator(value_list_validator &&) = default;                  //!< Defaulted.
    value_list_validator & operator=(value_list_validator const &) = default; //!< Defaulted.
    value_list_validator & operator=(value_list_validator &&) = default;      //!< Defaulted.
    ~value_list_validator() = default;                                        //!< Defaulted.

    /*!\brief Constructing from a range.
     * \tparam range_type The type of range; must model std::ranges::forward_range and value_list_validator::option_value_type
     *                    must be constructible from the rvalue reference type of the given range.
     * \param[in] rng The range of valid values to test.
     */
    template <std::ranges::forward_range range_type>
    //!\cond
        requires std::constructible_from<option_value_type, std::ranges::range_rvalue_reference_t<range_type>>
    //!\endcond
    value_list_validator(range_type rng)
    {
        values.clear();
        std::move(rng.begin(), rng.end(), std::back_inserter(values));
    }

    /*!\brief Constructing from a parameter pack.
     * \tparam option_types The type of option values in the parameter pack; The value_list_validator::option_value_type must
     *                      be constructible from each type in the parameter pack.
     * \param[in] opts The parameter pack values.
     */
    template <typename ...option_types>
    //!\cond
        requires ((std::constructible_from<option_value_type, option_types> && ...))
    //!\endcond
    value_list_validator(option_types && ...opts)
    {
        (values.emplace_back(std::forward<option_types>(opts)), ...);
    }
    //!\}

    /*!\brief Tests whether cmp lies inside values.
     * \param cmp The input value to check.
     * \throws sharg::validation_error
     */
    void operator()(option_value_type const & cmp) const
    {
        if (!(std::find(values.begin(), values.end(), cmp) != values.end()))
            throw validation_error{detail::to_string("Value ", cmp, " is not one of ", values,".")};
    }

    /*!\brief Tests whether every element in \p range lies inside values.
     * \tparam range_type The type of range to check; must model std::ranges::forward_range.
     * \param  range      The input range to iterate over and check every element.
     * \throws sharg::validation_error
     */
    template <std::ranges::forward_range range_type>
    //!\cond
        requires std::convertible_to<std::ranges::range_value_t<range_type>, option_value_type>
    //!\endcond
    void operator()(range_type const & range) const
    {
        std::for_each(std::ranges::begin(range), std::ranges::end(range), [&] (auto cmp) { (*this)(cmp); });
    }

    //!\brief Returns a message that can be appended to the (positional) options help page info.
    std::string get_help_page_message() const
    {
        return detail::to_string("Value must be one of ", values, ".");
    }

private:
    //!\brief Minimum of the range to test.
    std::vector<option_value_type> values{};
};

/*!\brief Type deduction guides
 * \relates sharg::value_list_validator
 * \{
 */
//!\brief Given a parameter pack of types that are convertible to std::string, delegate to value type std::string.
template <typename option_type, typename ...option_types>
//!\cond
    requires (std::constructible_from<std::string, std::decay_t<option_types>> && ... &&
              std::constructible_from<std::string, std::decay_t<option_type>>)
//!\endcond
value_list_validator(option_type, option_types...) -> value_list_validator<std::string>;

//!\brief Deduction guide for ranges over a value type convertible to std::string.
template <typename range_type>
//!\cond
    requires (std::ranges::forward_range<std::decay_t<range_type>> &&
              std::constructible_from<std::string, std::ranges::range_value_t<range_type>>)
//!\endcond
value_list_validator(range_type && rng) -> value_list_validator<std::string>;

//!\brief Deduction guide for a parameter pack.
template <typename option_type, typename ...option_types>
value_list_validator(option_type, option_types ...) -> value_list_validator<option_type>;

//!\brief Deduction guide for ranges.
template <typename range_type>
//!\cond
    requires (std::ranges::forward_range<std::decay_t<range_type>>)
//!\endcond
value_list_validator(range_type && rng) -> value_list_validator<std::ranges::range_value_t<range_type>>;
//!\}

/*!\brief An abstract base class for the file and directory validators.
 * \ingroup argument_parser
 *
 * \details
 *
 * This class provides a common interface for sharg::input_file_validator and the sharg::output_file_validator as
 * well as the sharg::input_directory_validator and sharg::output_directory_validator.
 *
 * The type can be further specialised for the sharg::input_file_validator and the sharg::output_file_validator
 * using the template argument to determine the valid extensions from the given file type.
 *
 * \remark For a complete overview, take a look at \ref argument_parser
 */
class file_validator_base
{
public:

    //!\brief Type of values that are tested by validator.
    using option_value_type = std::string;

    /*!\name Constructors, destructor and assignment
     * \{
     */
    file_validator_base() = default;                                        //!< Defaulted.
    file_validator_base(file_validator_base const &) = default;             //!< Defaulted.
    file_validator_base(file_validator_base &&) = default;                  //!< Defaulted.
    file_validator_base & operator=(file_validator_base const &) = default; //!< Defaulted.
    file_validator_base & operator=(file_validator_base &&) = default;      //!< Defaulted.
    virtual ~file_validator_base() = default;                               //!< Virtual destructor.
    //!\}

    /*!\brief Tests if the given path is a valid input, respectively output, file or directory.
     * \param path The path to validate.
     *
     * \details
     *
     * This is a pure virtual function and must be overloaded by the derived class.
     */
    virtual void operator()(std::filesystem::path const & path) const = 0;

    /*!\brief Tests whether every path in list \p v passes validation. See operator()(option_value_type const & value)
     *        for further information.
     * \tparam range_type The type of range to check; must model std::ranges::forward_range and the value type must
     *                    be convertible to std::filesystem::path.
     * \param  v          The input range to iterate over and check every element.
     * \throws sharg::validation_error
     */
    template <std::ranges::forward_range range_type>
    //!\cond
        requires (std::convertible_to<std::ranges::range_value_t<range_type>, std::filesystem::path const &>
                 && !std::convertible_to<range_type, std::filesystem::path const &>)
    //!\endcond
    void operator()(range_type const & v) const
    {
         std::for_each(v.begin(), v.end(), [&] (auto cmp) { this->operator()(cmp); });
    }

protected:
    /*!\brief Validates the given filename path based on the specified extensions.
     * \param path The filename path.
     * \throws sharg::validation_error if the specified extensions don't match the given path, or
     *         std::filesystem::filesystem_error on underlying OS API errors.
     */
    void validate_filename(std::filesystem::path const & path) const
    {
        // If no valid extensions are given we can safely return here.
        if (extensions.empty())
            return;

        // Check if extension is available.
        if (!path.has_extension())
        {
            throw validation_error{"The given filename " + path.string() + " has no extension. Expected one of the "
                                   "following valid extensions:" + extensions_str + "!"};
        }

        std::string file_path{path.filename().string()};

        // Leading dot indicates a hidden file is not part of the extension.
        if (file_path.front() == '.')
            file_path.erase(0, 1);

        // Store a string_view containing all extensions for a better error message.
        std::string const all_extensions{file_path.substr(file_path.find(".") + 1)};

        // Compares the extensions in lower case.
        auto case_insensitive_ends_with = [&] (std::string const & ext)
        {
            return case_insensitive_string_ends_with(file_path, ext);
        };

        // Check if requested extension is present.
        if (std::find_if(extensions.begin(), extensions.end(), case_insensitive_ends_with) == extensions.end())
        {
            throw validation_error{"Expected one of the following valid extensions: " + extensions_str + "! Got " +
                                    all_extensions + " instead!"};
        }
    }

    /*!\brief Checks if the given path is readable.
     * \param path The path to check.
     * \throws sharg::validation_error if the path is not readable, or
     *         std::filesystem::filesystem_error on underlying OS API errors.
     */
    void validate_readability(std::filesystem::path const & path) const
    {
        // Check if input directory is readable.
        if (std::filesystem::is_directory(path))
        {
            std::error_code ec{};
            std::filesystem::directory_iterator{path, ec};  // if directory iterator cannot be created, ec will be set.
            if (static_cast<bool>(ec))
                throw validation_error{"Cannot read the directory \"" + path.string() + "\"!"};
        }
        else
        {
            // Must be a regular file.
            if (!std::filesystem::is_regular_file(path))
                throw validation_error{"Expected a regular file \"" + path.string() + "\"!"};

            std::ifstream file{path};
            if (!file.is_open() || !file.good())
                throw validation_error{"Cannot read the file \"" + path.string() + "\"!"};
        }
    }

    /*!\brief Checks if the given path is writable.
     * \param path The path to check.
     * \throws sharg::validation_error if the file could not be opened for writing, or
     *         std::filesystem::filesystem_error on underlying OS API errors.
     */
    void validate_writeability(std::filesystem::path const & path) const
    {
        std::ofstream file{path};
        sharg::detail::safe_filesystem_entry file_guard{path};

        bool is_open = file.is_open();
        bool is_good = file.good();
        file.close();

        if (!is_good || !is_open)
            throw validation_error{"Cannot write \"" + path.string() + "\"!"};

        file_guard.remove();
    }

    //!\brief Returns the information of valid file extensions.
    std::string valid_extensions_help_page_message() const
    {
        if (extensions.empty())
            return "";
        else
            return "Valid file extensions are: " + extensions_str + ".";
    }

    /*!\brief Helper function that checks if a string is a suffix of another string. Case insensitive.
     * \param str The string to be searched.
     * \param suffix The suffix to be searched for.
     * \returns `true` if `suffix` is a suffix of `str`, otherwise `false`.
     */
    bool case_insensitive_string_ends_with(std::string_view str, std::string_view suffix) const
    {
        size_t const suffix_length{suffix.size()};
        size_t const str_length{str.size()};

        if (suffix_length > str_length)
            return false;

        for (size_t j = 0, s_start = str_length - suffix_length; j < suffix_length; ++j)
            if (std::tolower(str[s_start + j]) != std::tolower(suffix[j]))
                return false;

        return true;
    }

    //!\brief Stores the extensions.
    std::vector<std::string> extensions{};

    //!\brief The extension range as a std:;string for pretty printing.
    std::string extensions_str{};
};

/*!\brief A validator that checks if a given path is a valid input file.
 * \ingroup argument_parser
 * \implements sharg::validator
 *
 * \details
 *
 * On construction, the validator can receive a list (std::vector over std::string) of valid file extensions.
 * The class acts as a functor that throws a sharg::validation_error exception whenever a given filename's
 * extension (std::filesystem::path) is not in the given list of valid file extensions, if the file does not exist, or
 * if the file does not have the proper read permissions.
 *
 * \include test/snippet/argument_parser/validators_input_file.cpp
 *
 * The following snippet demonstrates the different ways to instantiate the sharg::input_file_validator.
 *
 * \include test/snippet/argument_parser/validators_input_file_ext_from_file.cpp
 *
 * \note The validator works on every type that can be implicitly converted to std::filesystem::path.
 *
 * \remark For a complete overview, take a look at \ref argument_parser
 */
class input_file_validator : public file_validator_base
{
public:

    // Import from base class.
    using typename file_validator_base::option_value_type;

    /*!\name Constructors, destructor and assignment
     * \{
     */

    input_file_validator() = default;                                         //!< Defaulted.
    input_file_validator(input_file_validator const &) = default;             //!< Defaulted.
    input_file_validator(input_file_validator &&) = default;                  //!< Defaulted.
    input_file_validator & operator=(input_file_validator const &) = default; //!< Defaulted.
    input_file_validator & operator=(input_file_validator &&) = default;      //!< Defaulted.
    virtual ~input_file_validator() = default;                                //!< Virtual destructor.

    /*!\brief Constructs from a given collection of valid extensions.
     * \param[in] extensions The valid extensions to validate for.
     */
    explicit input_file_validator(std::vector<std::string> extensions) : file_validator_base{}
    {
        file_validator_base::extensions_str = detail::to_string(extensions);
        file_validator_base::extensions = std::move(extensions);
    }

    // Import base class constructor.
    using file_validator_base::file_validator_base;
    //!\}

    // Import the base::operator()
    using file_validator_base::operator();

    /*!\brief Tests whether path is an existing regular file and is readable.
     * \param file The input value to check.
     * \throws sharg::validation_error if the validation process failed. Might be nested with
     *         std::filesystem::filesystem_error on unhandled OS API errors.
     */
    virtual void operator()(std::filesystem::path const & file) const override
    {
        try
        {
            if (!std::filesystem::exists(file))
                throw validation_error{"The file \"" + file.string() + "\" does not exist!"};

            // Check if file is regular and can be opened for reading.
            validate_readability(file);

            // Check extension.
            validate_filename(file);
        }
        // LCOV_EXCL_START
        catch (std::filesystem::filesystem_error & ex)
        {
            std::throw_with_nested(validation_error{"Unhandled filesystem error!"});
        }
        // LCOV_EXCL_STOP
        catch (...)
        {
            std::rethrow_exception(std::current_exception());
        }
    }

    //!\brief Returns a message that can be appended to the (positional) options help page info.
    std::string get_help_page_message() const
    {
        return "The input file must exist and read permissions must be granted." +
               ((valid_extensions_help_page_message().empty()) ? std::string{} : std::string{" "}) +
               valid_extensions_help_page_message();
    }
};

//!\brief Mode of an output file: Determines whether an existing file can be (silently) overwritten.
enum class output_file_open_options
{
    //!\brief Allow to overwrite the output file
    open_or_create,
    //!\brief Forbid overwriting the output file
    create_new
};

/*!\brief A validator that checks if a given path is a valid output file.
 * \ingroup argument_parser
 * \implements sharg::validator
 *
 * \details
 *
 * On construction, the validator can receive a list (std::vector over std::string) of valid file extensions.
 * The class acts as a functor that throws a sharg::validation_error exception whenever a given filename's extension
 * (std::string) is not in the given list of valid file extensions, or if the parent path does not have the proper
 * writer permissions.
 * In addition, the validator receives a sharg::output_file_open_options which allows you to specify what to do if your
 * output file already exists. sharg::output_file_open_options::create_new will throw a sharg::validation_error
 * exception if it already exists and sharg::output_file_open_options::open_or_create will skip this check (that means
 * you are allowed to overwrite the existing file).
 *
 * \include test/snippet/argument_parser/validators_output_file.cpp
 *
 * The following snippet demonstrates the different ways to instantiate the sharg::output_file_validator.
 *
 * \include test/snippet/argument_parser/validators_output_file_ext_from_file.cpp
 *
 * \note The validator works on every type that can be implicitly converted to std::filesystem::path.
 *
 * \remark For a complete overview, take a look at \ref argument_parser
 */
class output_file_validator : public file_validator_base
{
public:

    // Import from base class.
    using typename file_validator_base::option_value_type;

    /*!\name Constructors, destructor and assignment
     * \{
     */

    //!\copydoc sharg::input_file_validator::input_file_validator()
    output_file_validator() : output_file_validator{output_file_open_options::create_new} {}

    output_file_validator(output_file_validator const &) = default;             //!< Defaulted.
    output_file_validator(output_file_validator &&) = default;                  //!< Defaulted.
    output_file_validator & operator=(output_file_validator const &) = default; //!< Defaulted.
    output_file_validator & operator=(output_file_validator &&) = default;      //!< Defaulted.
    virtual ~output_file_validator() = default; //!< Virtual Destructor.

    /*!\brief Constructs from a given overwrite mode and a list of valid extensions.
     * \param[in] mode A sharg::output_file_open_options indicating whether the validator throws if a file already
                       exists.
     * \param[in] extensions The valid extensions to validate for.
     */
    explicit output_file_validator(output_file_open_options const mode, std::vector<std::string> extensions = {})
        : file_validator_base{}, mode{mode}
    {
        file_validator_base::extensions_str = detail::to_string(extensions);
        file_validator_base::extensions = std::move(extensions);
    }

    // Import base constructor.
    using file_validator_base::file_validator_base;
    //!\}

    // Import the base::operator()
    using file_validator_base::operator();

    /*!\brief Tests whether path is does not already exists and is writable.
     * \param file The input value to check.
     * \throws sharg::validation_error if the validation process failed. Might be nested with
     *         std::filesystem::filesystem_error on unhandled OS API errors.
     */
    virtual void operator()(std::filesystem::path const & file) const override
    {
        try
        {
            if ((mode == output_file_open_options::create_new) && std::filesystem::exists(file))
                throw validation_error{"The file \"" + file.string() + "\" already exists!"};

            // Check if file has any write permissions.
            validate_writeability(file);

            validate_filename(file);
        }
        // LCOV_EXCL_START
        catch (std::filesystem::filesystem_error & ex)
        {
            std::throw_with_nested(validation_error{"Unhandled filesystem error!"});
        }
        // LCOV_EXCL_STOP
        catch (...)
        {
            std::rethrow_exception(std::current_exception());
        }
    }

    //!\brief Returns a message that can be appended to the (positional) options help page info.
    std::string get_help_page_message() const
    {
        if (mode == output_file_open_options::open_or_create)
        {
            return "Write permissions must be granted." +
                   ((valid_extensions_help_page_message().empty()) ? std::string{} : std::string{" "}) +
                   valid_extensions_help_page_message();

        }
        else // mode == create_new
        {
            return "The output file must not exist already and write permissions must be granted." +
                   ((valid_extensions_help_page_message().empty()) ? std::string{} : std::string{" "}) +
                   valid_extensions_help_page_message();

        }
    }

private:
    //!\brief Stores the current mode of whether it is valid to overwrite the output file.
    output_file_open_options mode{output_file_open_options::create_new};
};

/*!\brief A validator that checks if a given path is a valid input directory.
 * \ingroup argument_parser
 * \implements sharg::validator
 *
 * \details
 *
 * The class acts as a functor that throws a sharg::validation_error exception whenever a given directory
 * (std::filesystem::path) does not exist, the specified path is not a directory, or if the directory is not
 * readable.
 *
 * \include test/snippet/argument_parser/validators_input_directory.cpp
 *
 * \note The validator works on every type that can be implicitly converted to std::filesystem::path.
 *
 * \remark For a complete overview, take a look at \ref argument_parser
 */
class input_directory_validator : public file_validator_base
{
public:
    // Import from base class.
    using typename file_validator_base::option_value_type;

    /*!\name Constructors, destructor and assignment
     * \{
     */
    input_directory_validator() = default;                                              //!< Defaulted.
    input_directory_validator(input_directory_validator const &) = default;             //!< Defaulted.
    input_directory_validator(input_directory_validator &&) = default;                  //!< Defaulted.
    input_directory_validator & operator=(input_directory_validator const &) = default; //!< Defaulted.
    input_directory_validator & operator=(input_directory_validator &&) = default;      //!< Defaulted.
    virtual ~input_directory_validator() = default;                                     //!< Virtual Destructor.

    // Import base constructor.
    using file_validator_base::file_validator_base;
    //!\}

    // Import the base::operator()
    using file_validator_base::operator();

    /*!\brief Tests whether path is an existing directory and is readable.
     * \param dir The input value to check.
     * \throws sharg::validation_error if the validation process failed. Might be nested with
     *         std::filesystem::filesystem_error on unhandled OS API errors.
     */
    virtual void operator()(std::filesystem::path const & dir) const override
    {
        try
        {
            if (!std::filesystem::exists(dir))
                throw validation_error{"The directory \"" + dir.string() + "\" does not exists!"};

            if (!std::filesystem::is_directory(dir))
                throw validation_error{"The path \"" + dir.string() + "\" is not a directory!"};

            // Check if directory has any read permissions.
            validate_readability(dir);
        }
        // LCOV_EXCL_START
        catch (std::filesystem::filesystem_error & ex)
        {
            std::throw_with_nested(validation_error{"Unhandled filesystem error!"});
        }
        // LCOV_EXCL_STOP
        catch (...)
        {
            std::rethrow_exception(std::current_exception());
        }
    }

    //!\brief Returns a message that can be appended to the (positional) options help page info.
    std::string get_help_page_message() const
    {
        return "An existing, readable path for the input directory.";
    }
};

/*!\brief A validator that checks if a given path is a valid output directory.
 * \ingroup argument_parser
 * \implements sharg::validator
 *
 * \details
 *
 * The class acts as a functor that throws a sharg::validation_error exception whenever a given path
 * (std::filesystem::path) is not writable. This can happen if either the parent path does not exists, or the
 * path doesn't have the proper write permissions.
 *
 * \include test/snippet/argument_parser/validators_output_directory.cpp
 *
 * \note The validator works on every type that can be implicitly converted to std::filesystem::path.
 *
 * \remark For a complete overview, take a look at \ref argument_parser
 */
class output_directory_validator : public file_validator_base
{
public:
    // Imported from base class.
    using typename file_validator_base::option_value_type;

    /*!\name Constructors, destructor and assignment
     * \{
     */
    output_directory_validator() = default;                                               //!< Defaulted.
    output_directory_validator(output_directory_validator const &) = default;             //!< Defaulted.
    output_directory_validator(output_directory_validator &&) = default;                  //!< Defaulted.
    output_directory_validator & operator=(output_directory_validator const &) = default; //!< Defaulted.
    output_directory_validator & operator=(output_directory_validator &&) = default;      //!< Defaulted.
    virtual ~output_directory_validator() = default;                                      //!< Virtual Destructor.

    // Import base constructor.
    using file_validator_base::file_validator_base;
    //!\}

    // Import the base::operator().
    using file_validator_base::operator();

    /*!\brief Tests whether path is writable.
     * \param dir The input value to check.
     * \throws sharg::validation_error if the validation process failed. Might be nested with
     *         std::filesystem::filesystem_error on unhandled OS API errors.
     */
    virtual void operator()(std::filesystem::path const & dir) const override
    {
        bool dir_exists = std::filesystem::exists(dir);
        // Make sure the created dir is deleted after we are done.
        std::error_code ec;
        std::filesystem::create_directory(dir, ec); // does nothing and is not treated as error if path already exists.
        // if error code was set or if dummy.txt could not be created within the output dir, throw an error.
        if (static_cast<bool>(ec))
            throw validation_error{"Cannot create directory: \"" + dir.string() + "\"!"};

        try
        {
            if (!dir_exists)
            {
                sharg::detail::safe_filesystem_entry dir_guard{dir};
                validate_writeability(dir / "dummy.txt");
                dir_guard.remove_all();
            }
            else
            {
                validate_writeability(dir / "dummy.txt");
            }
        }
        // LCOV_EXCL_START
        catch (std::filesystem::filesystem_error & ex)
        {
            std::throw_with_nested(validation_error{"Unhandled filesystem error!"});
        }
        // LCOV_EXCL_STOP
        catch (...)
        {
            std::rethrow_exception(std::current_exception());
        }
    }

    //!\brief Returns a message that can be appended to the (positional) options help page info.
    std::string get_help_page_message() const
    {
        return "A valid path for the output directory.";
    }
};

/*!\brief A validator that checks if a matches a regular expression pattern.
 * \ingroup argument_parser
 * \implements sharg::validator
 *
 * \details
 *
 * On construction, the validator must receive a pattern for a regular expression.
 * The pattern variable will be used for constructing a std::regex and the
 * validator will call std::regex_match on the command line argument.
 * Note: A regex_match will only return true if the strings matches the pattern
 * completely (in contrast to regex_search which also matches substrings).
 *
 * The class than acts as a functor, that throws a sharg::validation_error
 * exception whenever string does not match the pattern.
 *
 * \include test/snippet/argument_parser/validators_4.cpp
 *
 * \remark For a complete overview, take a look at \ref argument_parser
 */
class regex_validator
{
public:
    //!\brief Type of values that are tested by validator.
    using option_value_type = std::string;

    /*!\brief Constructing from a vector.
     * \param[in] pattern_ The pattern to match.
     */
    regex_validator(std::string const & pattern_) :
        pattern{pattern_}
    {}

    /*!\brief Tests whether cmp lies inside values.
     * \param[in] cmp The value to validate.
     * \throws sharg::validation_error
     */
    void operator()(option_value_type const & cmp) const
    {
        std::regex rgx(pattern);
        if (!std::regex_match(cmp, rgx))
            throw validation_error{"Value " + cmp + " did not match the pattern " + pattern + "."};
    }

    /*!\brief Tests whether every filename in list v matches the pattern.
     * \tparam range_type The type of range to check; must model std::ranges::forward_range and the value type must
     *                    be convertible to std::string.
     * \param  v          The input range to iterate over and check every element.
     * \throws sharg::validation_error
     */
    template <std::ranges::forward_range range_type>
    //!\cond
        requires std::convertible_to<std::ranges::range_reference_t<range_type>, option_value_type const &>
    //!\endcond
    void operator()(range_type const & v) const
    {
        for (auto && file_name : v)
        {
            // note: we explicitly copy/construct any reference type other than `std::string &`
            (*this)(static_cast<option_value_type const &>(file_name));
        }
    }

    //!\brief Returns a message that can be appended to the (positional) options help page info.
    std::string get_help_page_message() const
    {
        return "Value must match the pattern '" + pattern + "'.";
    }

private:
    //!\brief The pattern to match.
    std::string pattern;
};

namespace detail
{

/*!\brief Validator that always returns true.
 * \ingroup argument_parser
 * \implements sharg::validator
 * \tparam option_value_t Must be a (container of) arithmetic type(s).
 *
 * \details
 *
 * The default validator is needed to make the validator parameter of
 * argument_parser::add_option and argument_parser::add_option optional.
 *
 * \remark For a complete overview, take a look at \ref argument_parser
 */
template <typename option_value_t>
struct default_validator
{
    //!\brief Type of values that are tested by validator
    using option_value_type = option_value_t;

    //!\brief Value cmp always passes validation because the operator never throws.
    void operator()(option_value_t const & /*cmp*/) const noexcept
    {}

    //!\brief Since no validation is happening the help message is empty.
    std::string get_help_page_message() const
    {
        return "";
    }
};

/*!\brief A helper struct to chain validators recursively via the pipe operator.
 *\ingroup argument_parser
 *\implements sharg::validator
 *
 *\details
 *
 * Note that both validators must operate on the same option_value_type in order to
 * avoid unexpected behaviour and ensure that the sharg::argument_parser::add_option
 * call is well-formed. (add_option(val, ...., validator) requires
 * that val is of same type as validator::option_value_type).
 *
 * \remark For a complete overview, take a look at \ref argument_parser
 */
template <validator validator1_type, validator validator2_type>
//!\cond
    requires std::common_with<typename validator1_type::option_value_type, typename validator2_type::option_value_type>
//!\endcond
class validator_chain_adaptor
{
public:
    //!\brief The underlying type in both validators.
    using option_value_type = std::common_type_t<typename validator1_type::option_value_type,
                                                 typename validator2_type::option_value_type>;

    /*!\name Constructors, destructor and assignment
     * \{
     */
    validator_chain_adaptor() = delete;                                                //!< Deleted.
    validator_chain_adaptor(validator_chain_adaptor const & pf) = default;             //!< Defaulted.
    validator_chain_adaptor & operator=(validator_chain_adaptor const & pf) = default; //!< Defaulted.
    validator_chain_adaptor(validator_chain_adaptor &&) = default;                     //!< Defaulted.
    validator_chain_adaptor & operator=(validator_chain_adaptor &&) = default;         //!< Defaulted.

    /*!\brief Constructing from two validators.
     * \param[in] vali1_ Some validator to be chained to vali2_.
     * \param[in] vali2_ Another validator to be chained to vali1_.
     */
    validator_chain_adaptor(validator1_type vali1_, validator2_type vali2_) :
        vali1{std::move(vali1_)}, vali2{std::move(vali2_)}
    {}

    //!\brief The destructor.
    ~validator_chain_adaptor() = default;
    //!\}

    /*!\brief Calls the operator() of each validator on the value cmp.
     * \tparam cmp_type The type of value to validate; must be invokable with each of the validator members.
     * \param[in] cmp   The value to validate.
     *
     * This function delegates to the validation of both of the chained validators
     * by calling their operator() one after the other. The behaviour depends on
     * the chained validators which may throw on input error.
     */
    template <typename cmp_type>
    //!\cond
        requires std::invocable<validator1_type, cmp_type const> && std::invocable<validator2_type, cmp_type const>
    //!\endcond
    void operator()(cmp_type const & cmp) const
    {
        vali1(cmp);
        vali2(cmp);
    }

    //!\brief Returns a message that can be appended to the (positional) options help page info.
    std::string get_help_page_message() const
    {
        return vali1.get_help_page_message() + " " + vali2.get_help_page_message();
    }

private:
    //!\brief The first validator in the chain.
    validator1_type vali1;
    //!\brief The second validator in the chain.
    validator2_type vali2;
};

} // namespace detail

/*!\brief Enables the chaining of validators.
 * \ingroup argument_parser
 * \tparam validator1_type The type of the fist validator;
 *                         Must satisfy the sharg::validator and the
 *                         same option_value_type as the second validator type.
 * \tparam validator2_type The type of the second validator;
 *                         Must satisfy the sharg::validator and the
 *                         same option_value_type as the fist validator type.
 * \param[in] vali1 The first validator to chain.
 * \param[in] vali2 The second validator to chain.
 * \returns A new validator that tests a value for both vali1 and vali2.
 *
 * \details
 *
 * The pipe operator is the AND operation for two validators, which means that a
 * value must pass both validators in order to be accepted by the new validator.
 *
 * For example you may want a file name that only accepts absolute paths but
 * also must have one out of some given file extensions.
 * For this purpose you can chain a sharg::regex_validator to a
 * sharg::input_file_validator like this:
 *
 * \include test/snippet/argument_parser/validators_chaining.cpp
 *
 * You can chain as many validators as you want which will be evaluated one after
 * the other from left to right (first to last).
 *
 * \remark For a complete overview, take a look at \ref argument_parser
 */
template <validator validator1_type, validator validator2_type>
//!\cond
    requires std::common_with<typename std::remove_reference_t<validator1_type>::option_value_type,
                              typename std::remove_reference_t<validator2_type>::option_value_type>
//!\endcond
auto operator|(validator1_type && vali1, validator2_type && vali2)
{
    return detail::validator_chain_adaptor{std::forward<validator1_type>(vali1),
                                           std::forward<validator2_type>(vali2)};
}

} // namespace sharg
