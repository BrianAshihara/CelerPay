#pragma once

#include <celerpay/error.hpp>
#include <celerpay/pix.hpp>
#include <celerpay/qr_export.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace celerpay::qr {

enum class Ecc { Low, Medium, Quartile, High };

struct Options {
    Ecc ecc = Ecc::Medium;
    int minVersion = 1;
    int maxVersion = 40;
    /// -1 escolhe a máscara de menor penalidade. 0 a 7 força uma máscara.
    int mask = -1;
};

class QrMatrix {
public:
    int size() const noexcept { return size_; }
    int version() const noexcept { return version_; }
    Ecc ecc() const noexcept { return ecc_; }
    int mask() const noexcept { return mask_; }
    bool at(int x, int y) const noexcept { return modules_[static_cast<std::size_t>(y * size_ + x)] != 0; }

private:
    friend struct MatrixAccess;

    int size_ = 0;
    int version_ = 0;
    Ecc ecc_ = Ecc::Medium;
    int mask_ = 0;
    std::vector<std::uint8_t> modules_;
};

struct SvgOptions {
    int moduleSize = 8;
    int quietZone = 4;
    std::string_view dark = "#000000";
    /// Vazio deixa o fundo transparente.
    std::string_view light = "#FFFFFF";
};

/// Bytes que cabem em modo byte na versão e nível dados.
CELERPAY_QR_API int capacity(int version, Ecc ecc) noexcept;

CELERPAY_QR_API Result<QrMatrix> encode(std::string_view data, const Options& options = {});

CELERPAY_QR_API Result<std::string> toSvg(const QrMatrix& matrix, const SvgOptions& options = {});

/// pix::build, encode e toSvg em uma chamada.
CELERPAY_QR_API Result<std::string> pixToSvg(const pix::Payload& payload, const Options& options = {},
                                             const SvgOptions& svg = {});

}
