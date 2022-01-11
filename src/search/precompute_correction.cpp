// -----------------------------------------------------------------------------------------------------
// Copyright (c) 2006-2022, Knut Reinert & Freie Universität Berlin
// Copyright (c) 2016-2022, Knut Reinert & MPI für molekulare Genetik
// This file may be used, modified and/or redistributed under the terms of the 3-clause BSD-License
// shipped with this file and also available at: https://github.com/seqan/raptor/blob/master/LICENSE.md
// -----------------------------------------------------------------------------------------------------

/*!\file
 * \author Enrico Seiler <enrico.seiler AT fu-berlin.de>
 * \brief Provides stuff.
 */

#include <fstream>
#include <numeric>

#include <cereal/types/vector.hpp>

#include <raptor/search/detail/pascal_row.hpp>
#include <raptor/search/precompute_correction.hpp>

namespace raptor
{

std::string const correction_filename(search_arguments const & arguments)
{
    std::stringstream stream{};
    stream << "correction_"
           << std::hex
           << arguments.pattern_size
           << '_'
           << arguments.window_size
           << '_'
           << arguments.shape.to_ulong()
           << '_'
           << arguments.p_max
           << '_'
           << arguments.fpr
           << ".bin";
    std::string result = stream.str();
    if (auto it = result.find("0."); it != std::string::npos)
        result.replace(it, 2, "");
    if (auto it = result.find("0."); it != std::string::npos)
        result.replace(it, 2, "");
    return result;
}

void write_correction(std::vector<size_t> const & vec, search_arguments const & arguments)
{
    if (!arguments.cache_thresholds)
        return;

    std::filesystem::path filename = arguments.index_file.parent_path() / correction_filename(arguments);
    std::ofstream os{filename, std::ios::binary};
    cereal::BinaryOutputArchive oarchive{os};
    oarchive(vec);
}

bool read_correction(std::vector<size_t> & vec, search_arguments const & arguments)
{
    std::filesystem::path filename = arguments.index_file.parent_path() / correction_filename(arguments);
    if (!std::filesystem::exists(filename))
        return false;

    std::ifstream is{filename, std::ios::binary};
    cereal::BinaryInputArchive iarchive{is};
    iarchive(vec);
    return true;
}

std::vector<size_t> precompute_correction(search_arguments const & arguments)
{
    std::vector<size_t> correction;

    if (arguments.treshold_was_set || read_correction(correction, arguments))
        return correction;

    uint8_t const kmer_size{arguments.shape.size()};

    assert(arguments.window_size != kmer_size); // Only applicable to probabilistic thresholding.

    size_t const kmers_per_window = arguments.window_size - kmer_size + 1;
    size_t const kmers_per_pattern = arguments.pattern_size - kmer_size + 1;

    size_t const minimal_number_of_minimizers = kmers_per_pattern / kmers_per_window;
    size_t const maximal_number_of_minimizers = arguments.pattern_size - arguments.window_size + 1;

    correction.reserve(maximal_number_of_minimizers - minimal_number_of_minimizers);

    double const fpr{arguments.fpr};
    double const inv_fpr{1.0 - fpr};

    auto binom = [&fpr, &inv_fpr] (std::vector<size_t> const & binom_coeff,
                                       size_t const number_of_minimizers,
                                       size_t const number_of_fp)
    {
        return binom_coeff[number_of_fp] *
               std::pow(fpr, number_of_fp) *
               std::pow(inv_fpr, number_of_minimizers - number_of_fp);
    };

    // Iterate over the possible number of minimizers
    for (size_t number_of_minimizers = minimal_number_of_minimizers;
        number_of_minimizers <= maximal_number_of_minimizers;
        ++number_of_minimizers)
    {
        size_t number_of_fp{1u};
        std::vector<size_t> const binom_coeff = detail::pascal_row(number_of_minimizers);
        while (binom(binom_coeff, number_of_minimizers, number_of_fp) >= arguments.p_max)
            ++number_of_fp;

        correction.push_back(number_of_fp - 1);
    }
    assert(correction.size() != 0);

    write_correction(correction, arguments);

    return correction;
}

} // namespace raptor