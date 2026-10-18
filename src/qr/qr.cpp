#include <celerpay/qr.hpp>

#include <algorithm>
#include <array>
#include <climits>
#include <cstdlib>
#include <span>

using namespace std;

namespace celerpay::qr {

namespace {

using detail::fail;

constexpr int kMinVersion = 1;
constexpr int kMaxVersion = 40;

constexpr array<array<int8_t, 41>, 4> kEccPerBlock = {{
    {-1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28,
     28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
    {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26,
     26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28},
    {-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30,
     28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
    {-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28,
     30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
}};

constexpr array<array<int8_t, 41>, 4> kBlockCount = {{
    {-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8,
     8, 9, 9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25},
    {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16,
     17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49},
    {-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20,
     23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68},
    {-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25,
     25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81},
}};

constexpr int eccIndex(Ecc ecc) noexcept { return static_cast<int>(ecc); }

constexpr int formatBits(Ecc ecc) noexcept {
    switch (ecc) {
    case Ecc::Low:
        return 1;
    case Ecc::Medium:
        return 0;
    case Ecc::Quartile:
        return 3;
    case Ecc::High:
        return 2;
    }
    return 0;
}

constexpr int rawDataModules(int version) noexcept {
    int result = (16 * version + 128) * version + 64;
    if (version >= 2) {
        const int alignCount = version / 7 + 2;
        result -= (25 * alignCount - 10) * alignCount - 55;
        if (version >= 7) {
            result -= 36;
        }
    }
    return result;
}

constexpr int dataCodewords(int version, Ecc ecc) noexcept {
    const int e = eccIndex(ecc);
    return rawDataModules(version) / 8 - kEccPerBlock[static_cast<size_t>(e)][static_cast<size_t>(version)] *
                                             kBlockCount[static_cast<size_t>(e)][static_cast<size_t>(version)];
}

constexpr int countBits(int version) noexcept { return version <= 9 ? 8 : 16; }

struct Gf256 {
    array<uint8_t, 512> exp{};
    array<uint8_t, 256> log{};
};

constexpr Gf256 kGf = [] {
    Gf256 gf;
    int x = 1;
    for (int i = 0; i < 255; ++i) {
        gf.exp[static_cast<size_t>(i)] = static_cast<uint8_t>(x);
        gf.log[static_cast<size_t>(x)] = static_cast<uint8_t>(i);
        x <<= 1;
        if (x & 0x100) {
            x ^= 0x11D;
        }
    }
    for (size_t i = 255; i < gf.exp.size(); ++i) {
        gf.exp[i] = gf.exp[i - 255];
    }
    return gf;
}();

constexpr uint8_t gfMul(uint8_t a, uint8_t b) noexcept {
    if (a == 0 || b == 0) {
        return 0;
    }
    return kGf.exp[static_cast<size_t>(kGf.log[a]) + kGf.log[b]];
}

vector<uint8_t> rsDivisor(int degree) {
    vector<uint8_t> result(static_cast<size_t>(degree), 0);
    result.back() = 1;
    uint8_t root = 1;
    for (int i = 0; i < degree; ++i) {
        for (size_t j = 0; j < result.size(); ++j) {
            result[j] = gfMul(result[j], root);
            if (j + 1 < result.size()) {
                result[j] ^= result[j + 1];
            }
        }
        root = gfMul(root, 0x02);
    }
    return result;
}

vector<uint8_t> rsRemainder(span<const uint8_t> data, const vector<uint8_t>& divisor) {
    vector<uint8_t> result(divisor.size(), 0);
    for (const uint8_t byte : data) {
        const auto factor = static_cast<uint8_t>(byte ^ result.front());
        rotate(result.begin(), result.begin() + 1, result.end());
        result.back() = 0;
        for (size_t i = 0; i < result.size(); ++i) {
            result[i] ^= gfMul(divisor[i], factor);
        }
    }
    return result;
}

class BitBuffer {
public:
    void append(uint32_t value, int bits) {
        for (int i = bits - 1; i >= 0; --i) {
            bits_.push_back(static_cast<uint8_t>((value >> i) & 1));
        }
    }

    size_t size() const noexcept { return bits_.size(); }

    vector<uint8_t> bytes() const {
        vector<uint8_t> out(bits_.size() / 8, 0);
        for (size_t i = 0; i < bits_.size(); ++i) {
            out[i >> 3] = static_cast<uint8_t>(out[i >> 3] | (bits_[i] << (7 - (i & 7))));
        }
        return out;
    }

private:
    vector<uint8_t> bits_;
};

vector<uint8_t> encodeData(string_view data, int version, Ecc ecc) {
    const size_t capacityBits = static_cast<size_t>(dataCodewords(version, ecc)) * 8;
    BitBuffer bits;
    bits.append(0x4, 4);
    bits.append(static_cast<uint32_t>(data.size()), countBits(version));
    for (const char c : data) {
        bits.append(static_cast<unsigned char>(c), 8);
    }
    bits.append(0, static_cast<int>(min<size_t>(4, capacityBits - bits.size())));
    bits.append(0, static_cast<int>((8 - bits.size() % 8) % 8));
    for (uint32_t pad = 0xEC; bits.size() < capacityBits; pad ^= 0xEC ^ 0x11) {
        bits.append(pad, 8);
    }
    return bits.bytes();
}

vector<uint8_t> addEccAndInterleave(const vector<uint8_t>& data, int version, Ecc ecc) {
    const auto e = static_cast<size_t>(eccIndex(ecc));
    const int blockCount = kBlockCount[e][static_cast<size_t>(version)];
    const int eccLength = kEccPerBlock[e][static_cast<size_t>(version)];
    const int rawCodewords = rawDataModules(version) / 8;
    const int shortBlocks = blockCount - rawCodewords % blockCount;
    const int shortLength = rawCodewords / blockCount;

    const auto divisor = rsDivisor(eccLength);
    vector<vector<uint8_t>> blocks;
    size_t k = 0;
    for (int i = 0; i < blockCount; ++i) {
        const auto length = static_cast<size_t>(shortLength - eccLength + (i < shortBlocks ? 0 : 1));
        vector<uint8_t> block(data.begin() + static_cast<ptrdiff_t>(k), data.begin() + static_cast<ptrdiff_t>(k + length));
        k += length;
        const auto remainder = rsRemainder(block, divisor);
        if (i < shortBlocks) {
            block.push_back(0);
        }
        block.insert(block.end(), remainder.begin(), remainder.end());
        blocks.push_back(std::move(block));
    }

    vector<uint8_t> out;
    out.reserve(static_cast<size_t>(rawCodewords));
    for (size_t i = 0; i < blocks[0].size(); ++i) {
        for (size_t j = 0; j < blocks.size(); ++j) {
            if (i != static_cast<size_t>(shortLength - eccLength) || j >= static_cast<size_t>(shortBlocks)) {
                out.push_back(blocks[j][i]);
            }
        }
    }
    return out;
}

vector<int> alignmentPositions(int version) {
    if (version == 1) {
        return {};
    }
    const int count = version / 7 + 2;
    const int size = version * 4 + 17;
    const int step = (version * 8 + count * 3 + 5) / (count * 4 - 4) * 2;
    vector<int> out(static_cast<size_t>(count));
    out[0] = 6;
    for (int i = count - 1, pos = size - 7; i >= 1; --i, pos -= step) {
        out[static_cast<size_t>(i)] = pos;
    }
    return out;
}

class Canvas {
public:
    explicit Canvas(int version)
        : version_{version}, size_{version * 4 + 17},
          modules_(static_cast<size_t>(size_ * size_), 0), function_(static_cast<size_t>(size_ * size_), 0) {}

    int size() const noexcept { return size_; }

    bool get(int x, int y) const noexcept { return modules_[index(x, y)] != 0; }

    void setFunction(int x, int y, bool dark) noexcept {
        modules_[index(x, y)] = dark ? 1 : 0;
        function_[index(x, y)] = 1;
    }

    void drawFunctionPatterns() {
        for (int i = 0; i < size_; ++i) {
            setFunction(6, i, i % 2 == 0);
            setFunction(i, 6, i % 2 == 0);
        }
        drawFinder(3, 3);
        drawFinder(size_ - 4, 3);
        drawFinder(3, size_ - 4);

        const auto positions = alignmentPositions(version_);
        const size_t count = positions.size();
        for (size_t i = 0; i < count; ++i) {
            for (size_t j = 0; j < count; ++j) {
                const bool corner = (i == 0 && j == 0) || (i == 0 && j == count - 1) || (i == count - 1 && j == 0);
                if (!corner) {
                    drawAlignment(positions[i], positions[j]);
                }
            }
        }
        drawFormat(Ecc::Medium, 0);
        drawVersion();
    }

    void drawFormat(Ecc ecc, int mask) noexcept {
        const int data = formatBits(ecc) << 3 | mask;
        int remainder = data;
        for (int i = 0; i < 10; ++i) {
            remainder = (remainder << 1) ^ ((remainder >> 9) * 0x537);
        }
        const int bits = (data << 10 | remainder) ^ 0x5412;
        auto bit = [&](int i) { return ((bits >> i) & 1) != 0; };

        for (int i = 0; i <= 5; ++i) {
            setFunction(8, i, bit(i));
        }
        setFunction(8, 7, bit(6));
        setFunction(8, 8, bit(7));
        setFunction(7, 8, bit(8));
        for (int i = 9; i < 15; ++i) {
            setFunction(14 - i, 8, bit(i));
        }
        for (int i = 0; i < 8; ++i) {
            setFunction(size_ - 1 - i, 8, bit(i));
        }
        for (int i = 8; i < 15; ++i) {
            setFunction(8, size_ - 15 + i, bit(i));
        }
        setFunction(8, size_ - 8, true);
    }

    void drawData(const vector<uint8_t>& codewords) noexcept {
        size_t i = 0;
        const size_t totalBits = codewords.size() * 8;
        for (int right = size_ - 1; right >= 1; right -= 2) {
            if (right == 6) {
                right = 5;
            }
            for (int vertical = 0; vertical < size_; ++vertical) {
                for (int j = 0; j < 2; ++j) {
                    const int x = right - j;
                    const bool upward = ((right + 1) & 2) == 0;
                    const int y = upward ? size_ - 1 - vertical : vertical;
                    if (function_[index(x, y)] == 0 && i < totalBits) {
                        modules_[index(x, y)] = static_cast<uint8_t>((codewords[i >> 3] >> (7 - (i & 7))) & 1);
                        ++i;
                    }
                }
            }
        }
    }

    void applyMask(int mask) noexcept {
        for (int y = 0; y < size_; ++y) {
            for (int x = 0; x < size_; ++x) {
                if (function_[index(x, y)] == 0 && maskBit(mask, x, y)) {
                    modules_[index(x, y)] ^= 1;
                }
            }
        }
    }

    long penalty() const noexcept {
        long result = 0;
        for (int line = 0; line < size_; ++line) {
            result += linePenalty([&](int i) { return get(i, line); });
            result += linePenalty([&](int i) { return get(line, i); });
        }
        for (int y = 0; y + 1 < size_; ++y) {
            for (int x = 0; x + 1 < size_; ++x) {
                const bool c = get(x, y);
                if (c == get(x + 1, y) && c == get(x, y + 1) && c == get(x + 1, y + 1)) {
                    result += 3;
                }
            }
        }
        long dark = 0;
        for (const uint8_t m : modules_) {
            dark += m;
        }
        const long total = static_cast<long>(size_) * size_;
        const long k = (labs(dark * 20 - total * 10) + total - 1) / total - 1;
        result += k * 10;
        return result;
    }

    vector<uint8_t> take() && { return std::move(modules_); }

private:
    size_t index(int x, int y) const noexcept { return static_cast<size_t>(y * size_ + x); }

    void drawFinder(int cx, int cy) noexcept {
        for (int dy = -4; dy <= 4; ++dy) {
            for (int dx = -4; dx <= 4; ++dx) {
                const int x = cx + dx;
                const int y = cy + dy;
                if (x >= 0 && x < size_ && y >= 0 && y < size_) {
                    const int distance = max(abs(dx), abs(dy));
                    setFunction(x, y, distance != 2 && distance != 4);
                }
            }
        }
    }

    void drawAlignment(int cx, int cy) noexcept {
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                setFunction(cx + dx, cy + dy, max(abs(dx), abs(dy)) != 1);
            }
        }
    }

    void drawVersion() noexcept {
        if (version_ < 7) {
            return;
        }
        int remainder = version_;
        for (int i = 0; i < 12; ++i) {
            remainder = (remainder << 1) ^ ((remainder >> 11) * 0x1F25);
        }
        const long bits = static_cast<long>(version_) << 12 | remainder;
        for (int i = 0; i < 18; ++i) {
            const bool bit = ((bits >> i) & 1) != 0;
            const int a = size_ - 11 + i % 3;
            const int b = i / 3;
            setFunction(a, b, bit);
            setFunction(b, a, bit);
        }
    }

    static bool maskBit(int mask, int x, int y) noexcept {
        switch (mask) {
        case 0:
            return (x + y) % 2 == 0;
        case 1:
            return y % 2 == 0;
        case 2:
            return x % 3 == 0;
        case 3:
            return (x + y) % 3 == 0;
        case 4:
            return (x / 3 + y / 2) % 2 == 0;
        case 5:
            return x * y % 2 + x * y % 3 == 0;
        case 6:
            return (x * y % 2 + x * y % 3) % 2 == 0;
        default:
            return ((x + y) % 2 + x * y % 3) % 2 == 0;
        }
    }

    template <class Get>
    long linePenalty(Get&& get) const noexcept {
        long result = 0;
        int run = 1;
        for (int i = 1; i <= size_; ++i) {
            if (i < size_ && get(i) == get(i - 1)) {
                ++run;
                continue;
            }
            if (run >= 5) {
                result += 3 + (run - 5);
            }
            run = 1;
        }
        constexpr array<bool, 11> finderA = {true, false, true, true, true, false, true, false, false, false, false};
        constexpr array<bool, 11> finderB = {false, false, false, false, true, false, true, true, true, false, true};
        for (int i = 0; i + 11 <= size_; ++i) {
            bool matchA = true;
            bool matchB = true;
            for (int k = 0; k < 11; ++k) {
                const bool m = get(i + k);
                matchA = matchA && m == finderA[static_cast<size_t>(k)];
                matchB = matchB && m == finderB[static_cast<size_t>(k)];
            }
            result += (matchA ? 40 : 0) + (matchB ? 40 : 0);
        }
        return result;
    }

    int version_;
    int size_;
    vector<uint8_t> modules_;
    vector<uint8_t> function_;
};

bool isColorChar(char c) noexcept {
    return detail::isDigit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           string_view("#(),.% -").find(c) != string_view::npos;
}

Result<void> validateColor(string_view color) noexcept {
    for (size_t i = 0; i < color.size(); ++i) {
        if (!isColorChar(color[i])) {
            return fail(ErrorCode::InvalidCharacter, i, "cor com caractere não permitido");
        }
    }
    return {};
}

}

struct MatrixAccess {
    static QrMatrix make(int version, Ecc ecc, int mask, vector<uint8_t> modules) {
        QrMatrix m;
        m.size_ = version * 4 + 17;
        m.version_ = version;
        m.ecc_ = ecc;
        m.mask_ = mask;
        m.modules_ = std::move(modules);
        return m;
    }
};

int capacity(int version, Ecc ecc) noexcept {
    if (version < kMinVersion || version > kMaxVersion) {
        return 0;
    }
    const int bits = dataCodewords(version, ecc) * 8 - 4 - countBits(version);
    return bits / 8;
}

Result<QrMatrix> encode(string_view data, const Options& options) {
    if (options.minVersion < kMinVersion || options.maxVersion > kMaxVersion || options.minVersion > options.maxVersion) {
        return fail(ErrorCode::InvalidValue, 0, "versões devem estar entre 1 e 40");
    }
    if (options.mask < -1 || options.mask > 7) {
        return fail(ErrorCode::InvalidValue, 0, "máscara deve ser -1 ou de 0 a 7");
    }
    int version = options.minVersion;
    while (version <= options.maxVersion && static_cast<size_t>(capacity(version, options.ecc)) < data.size()) {
        ++version;
    }
    if (version > options.maxVersion) {
        return fail(ErrorCode::ValueTooLong, static_cast<size_t>(capacity(options.maxVersion, options.ecc)),
                    "dados não cabem na maior versão permitida");
    }

    const auto codewords = addEccAndInterleave(encodeData(data, version, options.ecc), version, options.ecc);
    Canvas base(version);
    base.drawFunctionPatterns();
    base.drawData(codewords);

    int bestMask = options.mask;
    if (bestMask < 0) {
        long bestPenalty = LONG_MAX;
        for (int mask = 0; mask < 8; ++mask) {
            Canvas candidate = base;
            candidate.applyMask(mask);
            candidate.drawFormat(options.ecc, mask);
            const long penalty = candidate.penalty();
            if (penalty < bestPenalty) {
                bestPenalty = penalty;
                bestMask = mask;
            }
        }
    }
    base.applyMask(bestMask);
    base.drawFormat(options.ecc, bestMask);
    return MatrixAccess::make(version, options.ecc, bestMask, std::move(base).take());
}

Result<string> toSvg(const QrMatrix& matrix, const SvgOptions& options) {
    if (options.moduleSize < 1 || options.moduleSize > 1000 || options.quietZone < 0 || options.quietZone > 100) {
        return fail(ErrorCode::InvalidValue, 0, "tamanho do módulo entre 1 e 1000, quiet zone entre 0 e 100");
    }
    if (const auto dark = validateColor(options.dark); !dark) {
        return unexpected(dark.error());
    }
    if (const auto light = validateColor(options.light); !light) {
        return unexpected(light.error());
    }

    const int total = matrix.size() + options.quietZone * 2;
    const string side = to_string(total);
    const string pixels = to_string(total * options.moduleSize);
    string out = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " + side + " " + side + "\" width=\"" + pixels +
                 "\" height=\"" + pixels + "\" shape-rendering=\"crispEdges\">";
    if (!options.light.empty()) {
        out += "<rect width=\"" + side + "\" height=\"" + side + "\" fill=\"" + string(options.light) + "\"/>";
    }
    out += "<path fill=\"" + string(options.dark) + "\" d=\"";
    for (int y = 0; y < matrix.size(); ++y) {
        for (int x = 0; x < matrix.size();) {
            if (!matrix.at(x, y)) {
                ++x;
                continue;
            }
            int run = 1;
            while (x + run < matrix.size() && matrix.at(x + run, y)) {
                ++run;
            }
            out += "M" + to_string(x + options.quietZone) + " " + to_string(y + options.quietZone) + "h" + to_string(run) +
                   "v1h-" + to_string(run) + "z";
            x += run;
        }
    }
    out += "\"/></svg>";
    return out;
}

Result<string> pixToSvg(const pix::Payload& payload, const Options& options, const SvgOptions& svg) {
    const auto built = pix::build(payload);
    if (!built) {
        return unexpected(built.error());
    }
    const auto matrix = encode(*built, options);
    if (!matrix) {
        return unexpected(matrix.error());
    }
    return toSvg(*matrix, svg);
}

}
