#include "rf/core/error.hpp"

#include <gtest/gtest.h>

#include <array>
#include <set>
#include <string>
#include <string_view>

namespace {

using rf::Errc;
using rf::Error;

constexpr std::array kAllCodes = {
    Errc::invalid_argument, Errc::not_found,      Errc::io_failure,
    Errc::permission_denied, Errc::unsupported_format, Errc::corrupt_data,
    Errc::decode_failure,   Errc::encode_failure, Errc::out_of_memory,
    Errc::device_lost,      Errc::cancelled,      Errc::timeout,
    Errc::version_mismatch, Errc::already_exists, Errc::internal,
};

TEST(Errc, EveryCodeHasADistinctName) {
    std::set<std::string_view> names;
    for (const Errc code : kAllCodes) {
        const std::string_view name = rf::to_string(code);
        EXPECT_NE(name, "unknown") << "code " << static_cast<int>(code) << " has no name";
        EXPECT_TRUE(names.insert(name).second) << "duplicate name: " << name;
    }
    EXPECT_EQ(names.size(), kAllCodes.size());
}

TEST(Errc, UnknownCodeDoesNotCrashLogging) {
    // Codes arrive from plugins and from serialized crash reports; an out-of-range
    // value must degrade, not take the logger down with it.
    const auto bogus = static_cast<Errc>(60000);
    EXPECT_EQ(rf::to_string(bogus), "unknown");
}

TEST(Errc, CodeValuesAreStable) {
    // These numbers appear in logs and crash reports from shipped builds and
    // must never be renumbered.
    EXPECT_EQ(static_cast<int>(Errc::invalid_argument), 1);
    EXPECT_EQ(static_cast<int>(Errc::not_found), 2);
    EXPECT_EQ(static_cast<int>(Errc::io_failure), 3);
    EXPECT_EQ(static_cast<int>(Errc::internal), 15);
}

TEST(Error, CarriesCodeAndMessage) {
    const Error e{Errc::io_failure, "read past end of file"};
    EXPECT_EQ(e.code(), Errc::io_failure);
    EXPECT_EQ(e.message(), "read past end of file");
}

TEST(Error, DefaultsMessageToCodeName) {
    const Error e{Errc::cancelled};
    EXPECT_EQ(e.message(), "cancelled");
}

TEST(Error, RecordsConstructionSite) {
    const Error e{Errc::internal, "invariant"};
    const std::string_view file = e.origin().file_name();
    EXPECT_NE(file.find("error_test.cpp"), std::string_view::npos) << "origin file: " << file;
    EXPECT_GT(e.origin().line(), 0u);
}

TEST(Error, ToStringIncludesCodeMessageAndOrigin) {
    const Error e{Errc::decode_failure, "no keyframe before pts 12345"};
    const std::string text = e.to_string();
    EXPECT_NE(text.find("decode_failure"), std::string::npos) << text;
    EXPECT_NE(text.find("no keyframe before pts 12345"), std::string::npos) << text;
    EXPECT_NE(text.find("error_test.cpp"), std::string::npos) << text;
}

TEST(Error, WithContextPrependsAndPreservesCodeAndOrigin) {
    const Error original{Errc::encode_failure, "muxer refused packet"};
    const Error contextual = original.with_context("export ig_reel");

    EXPECT_EQ(contextual.message(), "export ig_reel: muxer refused packet");
    EXPECT_EQ(contextual.code(), original.code());
    EXPECT_EQ(contextual.origin().line(), original.origin().line());
    EXPECT_STREQ(contextual.origin().file_name(), original.origin().file_name());
    // The original is unchanged: with_context is a pure function.
    EXPECT_EQ(original.message(), "muxer refused packet");
}

TEST(Error, WithContextNests) {
    const Error e = Error{Errc::io_failure, "EACCES"}
                        .with_context("open cache")
                        .with_context("project load");
    EXPECT_EQ(e.message(), "project load: open cache: EACCES");
}

TEST(Error, EqualityIgnoresOrigin) {
    const Error a{Errc::timeout, "gpu fence"};
    const Error b{Errc::timeout, "gpu fence"};
    const Error c{Errc::timeout, "other"};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

}  // namespace
