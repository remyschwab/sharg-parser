#include <sharg/validators.hpp>
#include <seqan3/core/debug_stream.hpp>
#include <seqan3/io/sequence_file/output.hpp>

int main()
{
    // Default constructed validator has an empty extension list.
    sharg::output_file_validator validator1{sharg::output_file_open_options::create_new};
    seqan3::debug_stream << validator1.get_help_page_message() << '\n';

    // Specify your own extensions for the output file.
    sharg::output_file_validator validator2{sharg::output_file_open_options::create_new,
                                             std::vector{std::string{"exe"}, std::string{"fasta"}}};
    seqan3::debug_stream << validator2.get_help_page_message() << '\n';

    // Give the seqan3 file type as a template argument to get all valid extensions for this file.
    sharg::output_file_validator<seqan3::sequence_file_output<>> validator3
    {
        sharg::output_file_open_options::create_new
    };
    seqan3::debug_stream << validator3.get_help_page_message() << '\n';

    return 0;
}
