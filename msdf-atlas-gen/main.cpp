
/*
* MULTI-CHANNEL SIGNED DISTANCE FIELD ATLAS GENERATOR - standalone console program
* 多通道有向距离场图集生成器 - 独立控制台程序
* --------------------------------------------------------------------------------
* A utility by Viktor Chlumsky, (c) 2020 - 2025
* 由 Viktor Chlumsky 开发的实用工具，(c) 2020 - 2025
*/

#ifdef MSDF_ATLAS_STANDALONE
#ifdef _WIN32
//由于 Windows 头文件中的 min 和 max 宏与 C++ 标准库中的 std::min 和 std::max 函数冲突会导致报错
//在包含 Windows 头文件之前定义 NOMINMAX，会阻止 Windows.h 定义 min 和 max 宏，从而避免与 C++ 标准库的冲突。
#define NOMINMAX
#include <windows.h>
#endif
#define _USE_MATH_DEFINES
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cassert>
#include <vector>
#include <algorithm>
#include <thread>

#include "msdf-atlas-gen.h"

using namespace msdf_atlas;

#define DEFAULT_SIZE 32.0
#define DEFAULT_ANGLE_THRESHOLD 3.0
#define DEFAULT_MITER_LIMIT 1.0
#define DEFAULT_PIXEL_RANGE 2.0
#define SDF_ERROR_ESTIMATE_PRECISION 19
#define GLYPH_FILL_RULE msdfgen::FILL_NONZERO
#define LCG_MULTIPLIER 6364136223846793005ull
#define LCG_INCREMENT 1442695040888963407ull

#define STRINGIZE_(x) #x
#define STRINGIZE(x) STRINGIZE_(x)
#define MSDF_ATLAS_VERSION_STRING STRINGIZE(MSDF_ATLAS_VERSION)
#define MSDFGEN_VERSION_STRING STRINGIZE(MSDFGEN_VERSION)
#ifdef MSDF_ATLAS_VERSION_UNDERLINE
    #define VERSION_UNDERLINE STRINGIZE(MSDF_ATLAS_VERSION_UNDERLINE)
#else
    #define VERSION_UNDERLINE "--------"
#endif

#ifdef MSDFGEN_USE_SKIA
    #define TITLE_SUFFIX    " & Skia"
    #define EXTRA_UNDERLINE "-------"
#else
    #define TITLE_SUFFIX
    #define EXTRA_UNDERLINE
#endif

/// 设置控制台使用UTF-8编码,解决Windows系统显示中文乱码
class CrossPlatformUTF8Console {
private:
#ifdef _WIN32
    UINT originalOutputCP{};
    UINT originalInputCP{};
#endif

public:
    CrossPlatformUTF8Console() {
#ifdef _WIN32
        // Windows: 保存并设置控制台编码
        originalOutputCP = GetConsoleOutputCP();
        originalInputCP = GetConsoleCP();
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
#else
        // Linux/macOS: 设置 UTF-8 locale
        std::setlocale(LC_ALL, "en_US.UTF-8");
        std::locale::global(std::locale("en_US.UTF-8"));
        std::cout.imbue(std::locale());
        std::cerr.imbue(std::locale());
#endif
    }

    ~CrossPlatformUTF8Console() {
#ifdef _WIN32
        // Windows: 恢复原始编码
        if (originalOutputCP != 0) SetConsoleOutputCP(originalOutputCP);
        if (originalInputCP != 0) SetConsoleCP(originalInputCP);
#endif
        // Linux/macOS: 不需要显式恢复，系统会自动处理
    }
};
/// 设置控制台使用UTF-8编码,解决Windows系统显示中文乱码

static const char *const versionText =
    "MSDF-Atlas-Gen 版本 " MSDF_ATLAS_VERSION_STRING "\n"
    "  基于 MSDFgen 版本 " MSDFGEN_VERSION_STRING TITLE_SUFFIX "\n"
    "(c) 2020 - " STRINGIZE(MSDF_ATLAS_COPYRIGHT_YEAR) " Viktor Chlumsky";

static const char *const helpText = R"(
MSDF 图集生成器 by Viktor Chlumsky 版本 )" MSDF_ATLAS_VERSION_STRING R"( (基于 MSDFgen 版本 )" MSDFGEN_VERSION_STRING TITLE_SUFFIX R"()
----------------------------------------------------------------)" VERSION_UNDERLINE EXTRA_UNDERLINE R"(

输入规范
  -font <文件名.ttf/otf>
      指定一个 TrueType / OpenType 字体文件。必须指定字体文件。)"
#ifndef MSDFGEN_DISABLE_VARIABLE_FONTS
R"(
  -varfont <文件名.ttf/otf?var0=value0&var1=value1>
      指定一个可变字体文件并配置其变量。)"
#endif
R"(
  -charset <文件名>
      指定输入的字符集文件。请参考文档了解字符集规范格式。默认为 ASCII。
  -glyphset <文件名>
      将输入的字符集指定为字体文件中的字形索引。
  -chars <字符集规范>
      内联指定输入字符集(即在命令行通过参数传递字符串)。请参考文档了解其语法。
  -glyphs <字形集规范>
      内联指定字形索引集。请参考文档了解其语法。
  -allglyphs
      指定处理字体文件中的所有字形。
  -fontscale <缩放比例>
      指定应用于字体字形几何的缩放比例。
  -fontname <名称>
      指定字体的名称，该名称将作为元数据传播到输出文件中。
  -and
      分隔多个输入，将它们组合到单个图集中。

图集配置
  -type <hardmask / softmask / sdf / psdf / msdf / mtsdf>
      选择要生成的图集类型。
)"
#ifndef MSDFGEN_DISABLE_PNG
R"(  -format <png / bmp / tiff / rgba / fl32 / text / textfloat / bin / binfloat / binfloatbe>)"
#else
R"(  -format <bmp / tiff / rgba / fl32 / text / textfloat / bin / binfloat / binfloatbe>)"
#endif
R"(
      选择图集图像的输出格式。某些图像格式可能与嵌入式输出格式不兼容。
  -dimensions <宽度> <高度>
      设置图集具有固定尺寸（宽度 x 高度）。
  -spacing <pixels>
      在图集中为每个字形周围添加指定像素的间距。
      这对于解决纹理采样时因像素插值导致的“边缘溢色”或“灰边”问题至关重要。推荐值为 1 或 2。
  -pots / -potr / -square / -square2 / -square4
      选择能够容纳所有字形并满足选定约束的最小图集尺寸：
      二次幂正方形 / 二次幂矩形 / 任意正方形 / 边长可被2整除的正方形 / 边长可被4整除的正方形
  -uniformgrid
      将图集布局为均匀网格。启用以下以 -uniform 开头的选项：
    -uniformcols <N>
        设置网格列数。
    -uniformcell <width> <height>
        设置网格单元的固定尺寸。
    -uniformcellconstraint <none / pots / potr / square / square2 / square4>
        将单元格尺寸约束到给定规则（参见上面的 -pots / ...）。
    -uniformorigin <off / on / horizontal / vertical>
        设置每个单元格中字形原点是否应固定在相同位置。
  -yorigin <bottom / top>
      确定 Y 轴是向上（底部原点，默认）还是向下（顶部原点）。

输出规范 - 可以指定一个或多个
  -imageout <文件名.*>
      将图集保存为指定格式的图像文件。布局数据必须单独存储。
  -json <文件名.json>
      将图集的布局数据以及其他指标写入结构化的 JSON 文件。
  -csv <文件名.csv>
      将字形的布局数据写入简单的 CSV 文件。)"
#ifndef MSDF_ATLAS_NO_ARTERY_FONT
R"(
  -arfont <文件名.arfont>
      将图集及其布局数据存储为 Artery Font 文件。支持的格式：png, bin, binfloat。)"
#endif
R"(
  -shadronpreview <文件名.shadron> <示例文本>
      生成一个 Shadron 脚本，使用生成的图集绘制示例文本作为预览。

字形配置
  -size <em尺寸>
      指定图集位图中字形的尺寸（像素每 em）。
  -minsize <em尺寸>
       指定最小尺寸。将使用适合相同图集尺寸的最大可能尺寸。
  -emrange <em范围宽度>
      指定可表示的 SDF 距离范围的宽度（以 em 为单位）。
  -pxrange <像素范围宽度>
      指定 SDF 距离范围的宽度（以输出像素为单位）。默认值为 2。
  -aemrange <最外层距离> <最内层距离>
      指定最外层（负）和最内层可表示距离（以 em 为单位）。
  -apxrange <最外层距离> <最内层距离>
      指定最外层（负）和最内层可表示距离（以像素为单位）。
  -pxalign <off / on / horizontal / vertical>
      指定每个字形的原点是否应与像素网格对齐。
  -nokerning
      在输出文件中禁用字距调整对表。
要指定每个字形的额外内部/外部填充（以 em或者像素为单位）：
  -empadding <宽度>
  -pxpadding <宽度>
  -outerempadding <宽度>
  -outerpxpadding <宽度>
或者为每条边指定单独的值来进行非对称填充：
  -aempadding <左> <下> <右> <上>
  -apxpadding <左> <下> <右> <上>
  -aouterempadding <左> <下> <右> <上>
  -aouterpxpadding <左> <下> <右> <上>

距离场生成器设置
  -angle <角度>
      指定相邻边之间的最小角度才能被视为转角。在数字后面附加 D 表示度数。（仅限 msdf 或者 mtsdf）
  -coloringstrategy <simple / inktrap / distance>
      选择边着色启发式策略。
  -errorcorrection <模式>
      更改 MSDF/MTSDF 错误修正模式。使用 -errorcorrection help 命令获取有效模式列表。
  -errordeviationratio <比率>
      设置实际距离增量与最大预期距离增量之间的最小比率才能被视为错误。
  -errorimproveratio <比率>
      设置修正前距离错误与修正后距离错误之间的最小比率。
  -miterlimit <值>
      设置斜接限制，限制由于非常尖锐的转角导致的每个字形边界框的扩展。（仅限 psdf / msdf / mtsdf）)"
#ifdef MSDFGEN_USE_SKIA
R"(
  -overlap
      切换到支持重叠轮廓的距离场生成器。
  -nopreprocess
      禁用路径预处理，该预处理可解析自相交和重叠轮廓。
  -scanline
      执行额外的扫描线传递以修复距离的符号。)"
#else
R"(
  -nooverlap
      D禁用重叠轮廓的解析。
  -noscanline
      禁用扫描线传递，该传递根据非零填充规则校正距离场的符号。)"
#endif
R"(
  -seed <N>
      设置边着色启发器的初始种子。
  -threads <N>
      设置并行计算的线程数。(0 表示自动)
)";

static const char *errorCorrectionHelpText = R"(
错误修正模式
  auto-fast
      通过范围测试检测反转伪影和不影响边缘的距离错误。
  auto-full
      通过精确距离评估检测反转伪影和不影响边缘的距离错误。
  auto-mixed (默认)
      通过距离评估检测反转，通过范围测试检测不影响边缘的距离错误。
  disabled
      禁用错误修正。
  distance-fast
      通过范围测试检测距离错误。不关心是否影响边缘和转角。
  distance-full
      通过精确距离评估检测距离错误。不关心是否影响边缘和转角，速度慢。
  edge-fast
       仅通过范围测试检测反转伪影。
  edge-full
      仅通过精确距离评估检测反转伪影。
  help
      显示此帮助信息。
)";

static char toupper(char c) {
    return c >= 'a' && c <= 'z' ? c-'a'+'A' : c;
}

static bool parseUnsigned(unsigned &value, const char *arg) {
    static char c;
    return sscanf(arg, "%u%c", &value, &c) == 1;
}

static bool parseUnsignedLL(unsigned long long &value, const char *arg) {
    static char c;
    return sscanf(arg, "%llu%c", &value, &c) == 1;
}

static bool parseDouble(double &value, const char *arg) {
    static char c;
    return sscanf(arg, "%lf%c", &value, &c) == 1;
}

static bool parseAngle(double &value, const char *arg) {
    char c1, c2;
    int result = sscanf(arg, "%lf%c%c", &value, &c1, &c2);
    if (result == 1)
        return true;
    if (result == 2 && (c1 == 'd' || c1 == 'D')) {
        value *= M_PI/180;
        return true;
    }
    return false;
}

static bool cmpExtension(const char *path, const char *ext) {
    for (const char *a = path+strlen(path)-1, *b = ext+strlen(ext)-1; b >= ext; --a, --b)
        if (a < path || toupper(*a) != toupper(*b))
            return false;
    return true;
}

static bool strStartsWith(const char *str, const char *prefix) {
    while (*prefix)
        if (*str++ != *prefix++)
            return false;
    return true;
}

#ifndef MSDFGEN_DISABLE_VARIABLE_FONTS
static msdfgen::FontHandle *loadVarFont(msdfgen::FreetypeHandle *library, const char *filename) {
    std::string buffer;
    while (*filename && *filename != '?')
        buffer.push_back(*filename++);
    msdfgen::FontHandle *font = msdfgen::loadFont(library, buffer.c_str());
    if (font && *filename++ == '?') {
        do {
            buffer.clear();
            while (*filename && *filename != '=')
                buffer.push_back(*filename++);
            if (*filename == '=') {
                double value = 0;
                int skip = 0;
                if (sscanf(++filename, "%lf%n", &value, &skip) == 1) {
                    msdfgen::setFontVariationAxis(library, font, buffer.c_str(), value);
                    filename += skip;
                }
            }
        } while (*filename++ == '&');
    }
    return font;
}
#endif

enum class Units {
    /// Value is specified in ems
    /// 值以 em 为单位指定
    EMS,
    /// Value is specified in pixels
    /// 值以像素为单位指定
    PIXELS
};

struct FontInput {
    const char *fontFilename;
    bool variableFont;
    GlyphIdentifierType glyphIdentifierType;
    const char *charsetFilename;
    const char *charsetString;
    double fontScale;
    const char *fontName;
};

struct Configuration {
    ImageType imageType;
    ImageFormat imageFormat;
    YDirection yDirection;
    int width, height;
    double emSize;
    msdfgen::Range pxRange;
    double angleThreshold;
    double miterLimit;
    bool pxAlignOriginX, pxAlignOriginY;
    struct {
        int cellWidth, cellHeight;
        int cols, rows;
        bool fixedOriginX, fixedOriginY;
    } grid;
    void (*edgeColoring)(msdfgen::Shape &, double, unsigned long long);
    bool expensiveColoring;
    unsigned long long coloringSeed;
    GeneratorAttributes generatorAttributes;
    bool preprocessGeometry;
    bool kerning;
    int threadCount;
    const char *arteryFontFilename;
    const char *imageFilename;
    const char *jsonFilename;
    const char *csvFilename;
    const char *shadronPreviewFilename;
    const char *shadronPreviewText;
};

template <typename T, typename S, int N, GeneratorFunction<S, N> GEN_FN>
static bool makeAtlas(const std::vector<GlyphGeometry> &glyphs, const std::vector<FontGeometry> &fonts, const Configuration &config) {
    ImmediateAtlasGenerator<S, N, GEN_FN, BitmapAtlasStorage<T, N> > generator(config.width, config.height);
    generator.setAttributes(config.generatorAttributes);
    generator.setThreadCount(config.threadCount);
    generator.generate(glyphs.data(), glyphs.size());
    msdfgen::BitmapConstRef<T, N> bitmap = (msdfgen::BitmapConstRef<T, N>) generator.atlasStorage();

    bool success = true;

    if (config.imageFilename) {
        if (saveImage(bitmap, config.imageFormat, config.imageFilename, config.yDirection))
            fputs("图集图像文件已保存。\n", stderr);
        else {
            success = false;
            fputs("无法将图集保存为图像文件。\n", stderr);
        }
    }

#ifndef MSDF_ATLAS_NO_ARTERY_FONT
    if (config.arteryFontFilename) {
        ArteryFontExportProperties arfontProps;
        arfontProps.fontSize = config.emSize;
        arfontProps.pxRange = config.pxRange;
        arfontProps.imageType = config.imageType;
        arfontProps.imageFormat = config.imageFormat;
        arfontProps.yDirection = config.yDirection;
        if (exportArteryFont<float>(fonts.data(), fonts.size(), bitmap, config.arteryFontFilename, arfontProps))
            fputs("Artery Font 文件已生成。\n", stderr);
        else {
            success = false;
            fputs("无法生成 Artery Font 文件。\n", stderr);
        }
    }
#endif

    return success;
}

int main(int argc, const char *const *argv) {
     // 创建跨平台 UTF-8 控制台管理器
    CrossPlatformUTF8Console consoleManager;

    #define ABORT(msg) do { fputs(msg "\n", stderr); return 1; } while (false)

    int result = 0;
    std::vector<FontInput> fontInputs;
    FontInput fontInput = { };
    Configuration config = { };
    fontInput.glyphIdentifierType = GlyphIdentifierType::UNICODE_CODEPOINT;
    fontInput.fontScale = -1;
    config.imageType = ImageType::MSDF;
    config.imageFormat = ImageFormat::UNSPECIFIED;
    config.yDirection = YDirection::BOTTOM_UP;
    config.grid.fixedOriginX = false, config.grid.fixedOriginY = true;
    config.edgeColoring = msdfgen::edgeColoringInkTrap;
    config.kerning = true;
    const char *imageFormatName = nullptr;
    int fixedWidth = -1, fixedHeight = -1;
    int fixedCellWidth = -1, fixedCellHeight = -1;

    // 添加一个变量来存储从命令行解析的间距值。
    // 初始化为 -1 表示用户没有通过命令行指定该值。
    int packingSpacing = -1;

    config.preprocessGeometry = (
        #ifdef MSDFGEN_USE_SKIA
            true
        #else
            false
        #endif
    );
    config.generatorAttributes.config.overlapSupport = !config.preprocessGeometry;
    config.generatorAttributes.scanlinePass = !config.preprocessGeometry;
    double minEmSize = 0;
    Units rangeUnits = Units::PIXELS;
    msdfgen::Range rangeValue = 0;
    Padding innerPadding;
    Padding outerPadding;
    Units innerPaddingUnits = Units::EMS;
    Units outerPaddingUnits = Units::EMS;
    PackingStyle packingStyle = PackingStyle::TIGHT;
    DimensionsConstraint atlasSizeConstraint = DimensionsConstraint::NONE;
    DimensionsConstraint cellSizeConstraint = DimensionsConstraint::NONE;
    config.angleThreshold = DEFAULT_ANGLE_THRESHOLD;
    config.miterLimit = DEFAULT_MITER_LIMIT;
    config.pxAlignOriginX = false, config.pxAlignOriginY = true;
    config.threadCount = 0;

    // Parse command line // 解析命令行
    int argPos = 1;
    bool suggestHelp = false;
    bool explicitErrorCorrectionMode = false;
    while (argPos < argc) {
        const char *arg = argv[argPos];
        #define ARG_CASE(s, p) if ((!strcmp(arg, s)) && argPos+(p) < argc && (++argPos, true))
        #define ARG_CASE_OR ) || !strcmp(arg,
        #define ARG_IS(s) (!strcmp(argv[argPos], s))
        #define ARG_PREFIX(s) strStartsWith(argv[argPos], s)

        // Accept arguments prefixed with -- instead of -
        // 接受以 -- 开头的参数而不是 -
        if (arg[0] == '-' && arg[1] == '-')
            ++arg;

        ARG_CASE("-type", 1) {
            if (ARG_IS("hardmask"))
                config.imageType = ImageType::HARD_MASK;
            else if (ARG_IS("softmask"))
                config.imageType = ImageType::SOFT_MASK;
            else if (ARG_IS("sdf"))
                config.imageType = ImageType::SDF;
            else if (ARG_IS("psdf"))
                config.imageType = ImageType::PSDF;
            else if (ARG_IS("msdf"))
                config.imageType = ImageType::MSDF;
            else if (ARG_IS("mtsdf"))
                config.imageType = ImageType::MTSDF;
            else
                ABORT("无效的图集类型。有效类型为：hardmask, softmask, sdf, psdf, msdf, mtsdf");
            ++argPos;
            continue;
        }
        ARG_CASE("-format", 1) {
            #ifndef MSDFGEN_DISABLE_PNG
                if (ARG_IS("png"))
                    config.imageFormat = ImageFormat::PNG;
                else
            #endif
            if (ARG_IS("bmp"))
                config.imageFormat = ImageFormat::BMP;
            else if (ARG_IS("tiff") || ARG_IS("tif"))
                config.imageFormat = ImageFormat::TIFF;
            else if (ARG_IS("rgba"))
                config.imageFormat = ImageFormat::RGBA;
            else if (ARG_IS("fl32"))
                config.imageFormat = ImageFormat::FL32;
            else if (ARG_IS("text") || ARG_IS("txt"))
                config.imageFormat = ImageFormat::TEXT;
            else if (ARG_IS("textfloat") || ARG_IS("txtfloat"))
                config.imageFormat = ImageFormat::TEXT_FLOAT;
            else if (ARG_IS("bin") || ARG_IS("binary"))
                config.imageFormat = ImageFormat::BINARY;
            else if (ARG_IS("binfloat") || ARG_IS("binfloatle"))
                config.imageFormat = ImageFormat::BINARY_FLOAT;
            else if (ARG_IS("binfloatbe"))
                config.imageFormat = ImageFormat::BINARY_FLOAT_BE;
            else {
                #ifndef MSDFGEN_DISABLE_PNG
                    ABORT("无效的图像格式。有效格式为：png, bmp, tiff, rgba, fl32, text, textfloat, bin, binfloat, binfloatbe");
                #else
                    ABORT("无效的图像格式。有效格式为：bmp, tiff, rgba, fl32, text, textfloat, bin, binfloat, binfloatbe");
                #endif
            }
            imageFormatName = arg;
            ++argPos;
            continue;
        }
        ARG_CASE("-font", 1) {
            fontInput.fontFilename = argv[argPos++];
            fontInput.variableFont = false;
            continue;
        }
    #ifndef MSDFGEN_DISABLE_VARIABLE_FONTS
        ARG_CASE("-varfont", 1) {
            fontInput.fontFilename = argv[argPos++];
            fontInput.variableFont = true;
            continue;
        }
    #endif
        ARG_CASE("-charset", 1) {
            fontInput.charsetFilename = argv[argPos++];
            fontInput.charsetString = nullptr;
            fontInput.glyphIdentifierType = GlyphIdentifierType::UNICODE_CODEPOINT;
            continue;
        }
        ARG_CASE("-glyphset", 1) {
            fontInput.charsetFilename = argv[argPos++];
            fontInput.charsetString = nullptr;
            fontInput.glyphIdentifierType = GlyphIdentifierType::GLYPH_INDEX;
            continue;
        }
        ARG_CASE("-chars", 1) {
            fontInput.charsetFilename = nullptr;
            fontInput.charsetString = argv[argPos++];
            fontInput.glyphIdentifierType = GlyphIdentifierType::UNICODE_CODEPOINT;
            continue;
        }
        ARG_CASE("-glyphs", 1) {
            fontInput.charsetFilename = nullptr;
            fontInput.charsetString = argv[argPos++];
            fontInput.glyphIdentifierType = GlyphIdentifierType::GLYPH_INDEX;
            continue;
        }
        ARG_CASE("-allglyphs", 0) {
            fontInput.charsetFilename = nullptr;
            fontInput.charsetString = nullptr;
            fontInput.glyphIdentifierType = GlyphIdentifierType::GLYPH_INDEX;
            continue;
        }
        ARG_CASE("-fontscale", 1) {
            double fs;
            if (!(parseDouble(fs, argv[argPos++]) && fs > 0))
                ABORT("无效的字体缩放参数。请使用 -fontscale <缩放比例> 并指定一个正实数。");
            fontInput.fontScale = fs;
            continue;
        }
        ARG_CASE("-fontname", 1) {
            fontInput.fontName = argv[argPos++];
            continue;
        }
        ARG_CASE("-and", 0) {
            if (!fontInput.fontFilename && !fontInput.charsetFilename && !fontInput.charsetString && fontInput.fontScale < 0)
                ABORT("-and 分隔符之前未指定字体、字符集或字体缩放比例。");
            if (!fontInputs.empty() && !memcmp(&fontInputs.back(), &fontInput, sizeof(FontInput)))
                ABORT("后续输入之间没有变化。必须在 -and 分隔符之间设置不同的字体、字符集或字体缩放比例。");
            fontInputs.push_back(fontInput);
            fontInput.fontName = nullptr;
            continue;
        }
    #ifndef MSDF_ATLAS_NO_ARTERY_FONT
        ARG_CASE("-arfont", 1) {
            config.arteryFontFilename = argv[argPos++];
            continue;
        }
    #endif
        ARG_CASE("-imageout", 1) {
            config.imageFilename = argv[argPos++];
            continue;
        }
        ARG_CASE("-json", 1) {
            config.jsonFilename = argv[argPos++];
            continue;
        }
        ARG_CASE("-csv", 1) {
            config.csvFilename = argv[argPos++];
            continue;
        }
        ARG_CASE("-shadronpreview", 2) {
            config.shadronPreviewFilename = argv[argPos++];
            config.shadronPreviewText = argv[argPos++];
            continue;
        }
        ARG_CASE("-dimensions", 2) {
            unsigned w, h;
            if (!(parseUnsigned(w, argv[argPos++]) && parseUnsigned(h, argv[argPos++]) && w && h))
                ABORT("无效的图集尺寸。请使用 -dimensions <宽度> <高度> 并指定两个正整数。");
            fixedWidth = w, fixedHeight = h;
            continue;
        }
        // 添加对新的 -spacing 参数的解析逻辑
        ARG_CASE("-spacing", 1) {// 如果当前参数是 -spacing，且后面还有至少1个参数（防止越界）
            unsigned s;
            if (!(parseUnsigned(s, argv[argPos++])))
                ABORT("无效的间距参数。请使用 -spacing <pixels> 并提供一个非负整数。");
            packingSpacing = s;
            continue;
        }
        ARG_CASE("-pots", 0) {
            atlasSizeConstraint = DimensionsConstraint::POWER_OF_TWO_SQUARE;
            fixedWidth = -1, fixedHeight = -1;
            continue;
        }
        ARG_CASE("-potr", 0) {
            atlasSizeConstraint = DimensionsConstraint::POWER_OF_TWO_RECTANGLE;
            fixedWidth = -1, fixedHeight = -1;
            continue;
        }
        ARG_CASE("-square", 0) {
            atlasSizeConstraint = DimensionsConstraint::SQUARE;
            fixedWidth = -1, fixedHeight = -1;
            continue;
        }
        ARG_CASE("-square2", 0) {
            atlasSizeConstraint = DimensionsConstraint::EVEN_SQUARE;
            fixedWidth = -1, fixedHeight = -1;
            continue;
        }
        ARG_CASE("-square4", 0) {
            atlasSizeConstraint = DimensionsConstraint::MULTIPLE_OF_FOUR_SQUARE;
            fixedWidth = -1, fixedHeight = -1;
            continue;
        }
        ARG_CASE("-yorigin", 1) {
            if (ARG_IS("bottom"))
                config.yDirection = YDirection::BOTTOM_UP;
            else if (ARG_IS("top"))
                config.yDirection = YDirection::TOP_DOWN;
            else
                ABORT("无效的 Y 轴原点。请使用 bottom 或 top。");
            ++argPos;
            continue;
        }
        ARG_CASE("-size", 1) {
            double s;
            if (!(parseDouble(s, argv[argPos++]) && s > 0))
                ABORT("无效的 em 尺寸参数。请使用 -size <em尺寸> 并指定一个正实数。");
            config.emSize = s;
            continue;
        }
        ARG_CASE("-minsize", 1) {
            double s;
            if (!(parseDouble(s, argv[argPos++]) && s > 0))
                ABORT("无效的最小 em 尺寸参数。请使用 -minsize <em 尺寸> 并指定一个正实数。");
            minEmSize = s;
            continue;
        }
        ARG_CASE("-emrange", 1) {
            double r;
            if (!(parseDouble(r, argv[argPos++]) && r != 0))
                ABORT("无效的范围参数。请使用 -emrange <em范围> 并指定一个非零实数。");
            rangeUnits = Units::EMS;
            rangeValue = r;
            continue;
        }
        ARG_CASE("-pxrange", 1) {
            double r;
            if (!(parseDouble(r, argv[argPos++]) && r != 0))
                ABORT("无效的范围参数。请使用 -pxrange <像素范围> 并指定一个非零实数。");
            rangeUnits = Units::PIXELS;
            rangeValue = r;
            continue;
        }
        ARG_CASE("-aemrange", 2) {
            double r0, r1;
            if (!(parseDouble(r0, argv[argPos++]) && parseDouble(r1, argv[argPos++])))
                ABORT("无效的范围参数。请使用 -aemrange <最小值> <最大值> 并指定两个实数。");
            if (r0 == r1)
                ABORT("范围必须非空。");
            rangeUnits = Units::EMS;
            rangeValue = msdfgen::Range(r0, r1);
            continue;
        }
        ARG_CASE("-apxrange", 2) {
            double r0, r1;
            if (!(parseDouble(r0, argv[argPos++]) && parseDouble(r1, argv[argPos++])))
                ABORT("无效的范围参数。请使用 -apxrange <最小值> <最大值> 并指定两个实数。");
            if (r0 == r1)
                ABORT("范围必须非空。");
            rangeUnits = Units::PIXELS;
            rangeValue = msdfgen::Range(r0, r1);
            continue;
        }
        ARG_CASE("-pxalign", 1) {
            if (ARG_IS("off") || ARG_PREFIX("disable") || ARG_IS("0") || ARG_IS("false") || ARG_PREFIX("n"))
                config.pxAlignOriginX = false, config.pxAlignOriginY = false;
            else if (ARG_IS("on") || ARG_PREFIX("enable") || ARG_IS("1") || ARG_IS("true") || ARG_IS("hv") || ARG_PREFIX("y"))
                config.pxAlignOriginX = true, config.pxAlignOriginY = true;
            else if (ARG_PREFIX("h"))
                config.pxAlignOriginX = true, config.pxAlignOriginY = false;
            else if (ARG_PREFIX("v") || ARG_IS("baseline") || ARG_IS("default"))
                config.pxAlignOriginX = false, config.pxAlignOriginY = true;
            else
                ABORT("未知的 -pxalign 设置。请使用以下之一：off, on, horizontal, vertical。");
            ++argPos;
            continue;
        }
        ARG_CASE("-empadding", 1) {
            double p;
            if (!parseDouble(p, argv[argPos++]))
                ABORT("无效的填充参数。请使用 -empadding <填充> 并指定一个实数。");
            innerPaddingUnits = Units::EMS;
            innerPadding = Padding(p);
            continue;
        }
        ARG_CASE("-pxpadding", 1) {
            double p;
            if (!parseDouble(p, argv[argPos++]))
                ABORT("无效的填充参数。请使用 -pxpadding <填充> 并指定一个实数。");
            innerPaddingUnits = Units::PIXELS;
            innerPadding = Padding(p);
            continue;
        }
        ARG_CASE("-outerempadding", 1) {
            double p;
            if (!parseDouble(p, argv[argPos++]))
                ABORT("无效的填充参数。请使用 -outerempadding <填充> 并指定一个实数。");
            outerPaddingUnits = Units::EMS;
            outerPadding = Padding(p);
            continue;
        }
        ARG_CASE("-outerpxpadding", 1) {
            double p;
            if (!parseDouble(p, argv[argPos++]))
                ABORT("无效的填充参数。请使用 -outerpxpadding <填充> 并指定一个实数。");
            outerPaddingUnits = Units::PIXELS;
            outerPadding = Padding(p);
            continue;
        }
        ARG_CASE("-aempadding", 4) {
            double l, b, r, t;
            if (!(parseDouble(l, argv[argPos++]) && parseDouble(b, argv[argPos++]) && parseDouble(r, argv[argPos++]) && parseDouble(t, argv[argPos++])))
                ABORT("无效的填充参数。请使用 -aempadding <左> <下> <右> <上> 并指定4个实数。");
            innerPaddingUnits = Units::EMS;
            innerPadding.l = l, innerPadding.b = b, innerPadding.r = r, innerPadding.t = t;
            continue;
        }
        ARG_CASE("-apxpadding", 4) {
            double l, b, r, t;
            if (!(parseDouble(l, argv[argPos++]) && parseDouble(b, argv[argPos++]) && parseDouble(r, argv[argPos++]) && parseDouble(t, argv[argPos++])))
                ABORT("无效的填充参数。请使用 -apxpadding <左> <下> <右> <上> 并指定4个实数。");
            innerPaddingUnits = Units::PIXELS;
            innerPadding.l = l, innerPadding.b = b, innerPadding.r = r, innerPadding.t = t;
            continue;
        }
        ARG_CASE("-aouterempadding", 4) {
            double l, b, r, t;
            if (!(parseDouble(l, argv[argPos++]) && parseDouble(b, argv[argPos++]) && parseDouble(r, argv[argPos++]) && parseDouble(t, argv[argPos++])))
                ABORT("无效的填充参数。请使用 -aouterempadding <左> <下> <右> <上> 并指定4个实数。");
            outerPaddingUnits = Units::EMS;
            outerPadding.l = l, outerPadding.b = b, outerPadding.r = r, outerPadding.t = t;
            continue;
        }
        ARG_CASE("-aouterpxpadding", 4) {
            double l, b, r, t;
            if (!(parseDouble(l, argv[argPos++]) && parseDouble(b, argv[argPos++]) && parseDouble(r, argv[argPos++]) && parseDouble(t, argv[argPos++])))
                ABORT("无效的填充参数。请使用 -aouterpxpadding <左> <下> <右> <上> 并指定4个实数。");
            outerPaddingUnits = Units::PIXELS;
            outerPadding.l = l, outerPadding.b = b, outerPadding.r = r, outerPadding.t = t;
            continue;
        }
        ARG_CASE("-angle", 1) {
            double at;
            if (!parseAngle(at, argv[argPos++]))
                ABORT("无效的角度阈值。请使用 -angle <最小角度> 并指定一个小于 PI 的正实数，或指定一个以度为单位并在后面加上 'd' 且小于 180d 的值。");
            config.angleThreshold = at;
            continue;
        }
        ARG_CASE("-uniformgrid", 0) {
            packingStyle = PackingStyle::GRID;
            continue;
        }
        ARG_CASE("-uniformcols", 1) {
            packingStyle = PackingStyle::GRID;
            unsigned c;
            if (!(parseUnsigned(c, argv[argPos++]) && c))
                ABORT("I无效的网格列数。请使用 -uniformcols <N> 并指定一个正整数。");
            config.grid.cols = c;
            continue;
        }
        ARG_CASE("-uniformcell", 2) {
            packingStyle = PackingStyle::GRID;
            unsigned w, h;
            if (!(parseUnsigned(w, argv[argPos++]) && parseUnsigned(h, argv[argPos++]) && w && h))
                ABORT("无效的单元格尺寸。请使用 -uniformcell <宽度> <高度> 并指定两个正整数。");
            fixedCellWidth = w, fixedCellHeight = h;
            continue;
        }
        ARG_CASE("-uniformcellconstraint", 1) {
            packingStyle = PackingStyle::GRID;
            if (ARG_IS("none") || ARG_IS("rect"))
                cellSizeConstraint = DimensionsConstraint::NONE;
            else if (ARG_IS("pots"))
                cellSizeConstraint = DimensionsConstraint::POWER_OF_TWO_SQUARE;
            else if (ARG_IS("potr"))
                cellSizeConstraint = DimensionsConstraint::POWER_OF_TWO_RECTANGLE;
            else if (ARG_IS("square"))
                cellSizeConstraint = DimensionsConstraint::SQUARE;
            else if (ARG_IS("square2"))
                cellSizeConstraint = DimensionsConstraint::EVEN_SQUARE;
            else if (ARG_IS("square4"))
                cellSizeConstraint = DimensionsConstraint::MULTIPLE_OF_FOUR_SQUARE;
            else
                ABORT("未知的尺寸约束。请使用 -uniformcellconstraint 并指定以下之一：none, pots, potr, square, square2, or square4.");
            ++argPos;
            continue;
        }
        ARG_CASE("-uniformorigin", 1) {
            packingStyle = PackingStyle::GRID;
            if (ARG_IS("off") || ARG_PREFIX("disable") || ARG_IS("0") || ARG_IS("false") || ARG_PREFIX("n"))
                config.grid.fixedOriginX = false, config.grid.fixedOriginY = false;
            else if (ARG_IS("on") || ARG_PREFIX("enable") || ARG_IS("1") || ARG_IS("true") || ARG_IS("hv") || ARG_PREFIX("y"))
                config.grid.fixedOriginX = true, config.grid.fixedOriginY = true;
            else if (ARG_PREFIX("h"))
                config.grid.fixedOriginX = true, config.grid.fixedOriginY = false;
            else if (ARG_PREFIX("v") || ARG_IS("baseline") || ARG_IS("default"))
                config.grid.fixedOriginX = false, config.grid.fixedOriginY = true;
            else
                ABORT("未知的 -uniformorigin 设置。请使用以下之一：off, on, horizontal, vertical.");
            ++argPos;
            continue;
        }
        ARG_CASE("-errorcorrection", 1) {
            msdfgen::ErrorCorrectionConfig &ec = config.generatorAttributes.config.errorCorrection;
            if (ARG_PREFIX("disable") || ARG_IS("0") || ARG_IS("none")) {
                ec.mode = msdfgen::ErrorCorrectionConfig::DISABLED;
                ec.distanceCheckMode = msdfgen::ErrorCorrectionConfig::DO_NOT_CHECK_DISTANCE;
            } else if (ARG_IS("default") || ARG_IS("auto") || ARG_IS("auto-mixed") || ARG_IS("mixed")) {
                ec.mode = msdfgen::ErrorCorrectionConfig::EDGE_PRIORITY;
                ec.distanceCheckMode = msdfgen::ErrorCorrectionConfig::CHECK_DISTANCE_AT_EDGE;
            } else if (ARG_IS("auto-fast") || ARG_IS("fast")) {
                ec.mode = msdfgen::ErrorCorrectionConfig::EDGE_PRIORITY;
                ec.distanceCheckMode = msdfgen::ErrorCorrectionConfig::DO_NOT_CHECK_DISTANCE;
            } else if (ARG_IS("auto-full") || ARG_IS("full")) {
                ec.mode = msdfgen::ErrorCorrectionConfig::EDGE_PRIORITY;
                ec.distanceCheckMode = msdfgen::ErrorCorrectionConfig::ALWAYS_CHECK_DISTANCE;
            } else if (ARG_IS("distance") || ARG_IS("distance-fast") || ARG_IS("indiscriminate") || ARG_IS("indiscriminate-fast")) {
                ec.mode = msdfgen::ErrorCorrectionConfig::INDISCRIMINATE;
                ec.distanceCheckMode = msdfgen::ErrorCorrectionConfig::DO_NOT_CHECK_DISTANCE;
            } else if (ARG_IS("distance-full") || ARG_IS("indiscriminate-full")) {
                ec.mode = msdfgen::ErrorCorrectionConfig::INDISCRIMINATE;
                ec.distanceCheckMode = msdfgen::ErrorCorrectionConfig::ALWAYS_CHECK_DISTANCE;
            } else if (ARG_IS("edge-fast")) {
                ec.mode = msdfgen::ErrorCorrectionConfig::EDGE_ONLY;
                ec.distanceCheckMode = msdfgen::ErrorCorrectionConfig::DO_NOT_CHECK_DISTANCE;
            } else if (ARG_IS("edge") || ARG_IS("edge-full")) {
                ec.mode = msdfgen::ErrorCorrectionConfig::EDGE_ONLY;
                ec.distanceCheckMode = msdfgen::ErrorCorrectionConfig::ALWAYS_CHECK_DISTANCE;
            } else if (ARG_IS("help")) {
                puts(errorCorrectionHelpText);
                return 0;
            } else
                ABORT("未知的错误修正模式。请使用 -errorcorrection help 命令获取更多信息。");
            ++argPos;
            explicitErrorCorrectionMode = true;
            continue;
        }
        ARG_CASE("-errordeviationratio", 1) {
            double edr;
            if (!(parseDouble(edr, argv[argPos++]) && edr > 0))
                ABORT("无效的错误偏差比率。请使用 -errordeviationratio <比率> 并指定一个正实数。");
            config.generatorAttributes.config.errorCorrection.minDeviationRatio = edr;
            continue;
        }
        ARG_CASE("-errorimproveratio", 1) {
            double eir;
            if (!(parseDouble(eir, argv[argPos++]) && eir > 0))
                ABORT("无效的错误改进比率。请使用 -errorimproveratio <比率> 并指定一个正实数。");
            config.generatorAttributes.config.errorCorrection.minImproveRatio = eir;
            continue;
        }
        ARG_CASE("-coloringstrategy" ARG_CASE_OR "-edgecoloring", 1) {
            if (ARG_IS("simple"))
                config.edgeColoring = &msdfgen::edgeColoringSimple, config.expensiveColoring = false;
            else if (ARG_IS("inktrap"))
                config.edgeColoring = &msdfgen::edgeColoringInkTrap, config.expensiveColoring = false;
            else if (ARG_IS("distance"))
                config.edgeColoring = &msdfgen::edgeColoringByDistance, config.expensiveColoring = true;
            else
                fputs("指定了未知的着色策略。\n", stderr);
            ++argPos;
            continue;
        }
        ARG_CASE("-miterlimit", 1) {
            double m;
            if (!(parseDouble(m, argv[argPos++]) && m >= 0))
                ABORT("无效的斜接限制参数。请使用 -miterlimit <限制> 并指定一个正实数。");
            config.miterLimit = m;
            continue;
        }
        ARG_CASE("-nokerning", 0) {
            config.kerning = false;
            continue;
        }
        ARG_CASE("-kerning", 0) {
            config.kerning = true;
            continue;
        }
        ARG_CASE("-nopreprocess", 0) {
            config.preprocessGeometry = false;
            continue;
        }
        ARG_CASE("-preprocess", 0) {
            config.preprocessGeometry = true;
            continue;
        }
        ARG_CASE("-nooverlap", 0) {
            config.generatorAttributes.config.overlapSupport = false;
            continue;
        }
        ARG_CASE("-overlap", 0) {
            config.generatorAttributes.config.overlapSupport = true;
            continue;
        }
        ARG_CASE("-noscanline", 0) {
            config.generatorAttributes.scanlinePass = false;
            continue;
        }
        ARG_CASE("-scanline", 0) {
            config.generatorAttributes.scanlinePass = true;
            continue;
        }
        ARG_CASE("-seed", 1) {
            if (!parseUnsignedLL(config.coloringSeed, argv[argPos++]))
                ABORT("无效的种子。请使用 -seed <N> 并指定 N 为一个非负整数。");
            continue;
        }
        ARG_CASE("-threads", 1) {
            unsigned tc;
            if (!(parseUnsigned(tc, argv[argPos++]) && (int) tc >= 0))
                ABORT("无效的线程数。请使用 -threads <N> 并指定 N 为一个非负整数。");
            config.threadCount = (int) tc;
            continue;
        }
        ARG_CASE("-version", 0) {
            puts(versionText);
            return 0;
        }
        ARG_CASE("-help", 0) {
            puts(helpText);
            return 0;
        }
        fprintf(stderr, "未知设置或参数不足： %s\n", argv[argPos++]);
        suggestHelp = true;
    }
    if (suggestHelp)
        fputs("使用 -help 获取更多信息。\n", stderr);

    // Nothing to do?
    if (argc == 1) {
        fputs(
            "用法： msdf-atlas-gen"
            #ifdef _WIN32
                ".exe"
            #endif
            " -font <文件名.ttf/otf> -charset <字符集> <输出规范> <选项>\n"
            "使用 -help 获取更多信息。\n",
            stderr
        );
        return 0;
    }
    if (!fontInput.fontFilename)
        ABORT("未指定字体文件。");
    if (!(config.arteryFontFilename || config.imageFilename || config.jsonFilename || config.csvFilename || config.shadronPreviewFilename)) {
        fputs("未指定输出文件。\n", stderr);
        return 0;
    }
    bool layoutOnly = !(config.arteryFontFilename || config.imageFilename);

    // Finalize font inputs // 完成字体输入
    const FontInput *nextFontInput = &fontInput;
    for (std::vector<FontInput>::reverse_iterator it = fontInputs.rbegin(); it != fontInputs.rend(); ++it) {
        if (!it->fontFilename && nextFontInput->fontFilename)
            it->fontFilename = nextFontInput->fontFilename;
        if (!(it->charsetFilename || it->charsetString || it->glyphIdentifierType == GlyphIdentifierType::GLYPH_INDEX) && (nextFontInput->charsetFilename || nextFontInput->charsetString || nextFontInput->glyphIdentifierType == GlyphIdentifierType::GLYPH_INDEX)) {
            it->charsetFilename = nextFontInput->charsetFilename;
            it->charsetString = nextFontInput->charsetString;
            it->glyphIdentifierType = nextFontInput->glyphIdentifierType;
        }
        if (it->fontScale < 0 && nextFontInput->fontScale >= 0)
            it->fontScale = nextFontInput->fontScale;
        nextFontInput = &*it;
    }
    if (fontInputs.empty() || memcmp(&fontInputs.back(), &fontInput, sizeof(FontInput)))
        fontInputs.push_back(fontInput);

    // Fix up configuration based on related values // 根据相关值修复配置
    if (packingStyle == PackingStyle::TIGHT && atlasSizeConstraint == DimensionsConstraint::NONE)
        atlasSizeConstraint = DimensionsConstraint::MULTIPLE_OF_FOUR_SQUARE;
    if (!(config.imageType == ImageType::PSDF || config.imageType == ImageType::MSDF || config.imageType == ImageType::MTSDF))
        config.miterLimit = 0;
    if (config.emSize > minEmSize)
        minEmSize = config.emSize;
    if (!(fixedWidth > 0 && fixedHeight > 0) && !(fixedCellWidth > 0 && fixedCellHeight > 0) && !(minEmSize > 0)) {
        fputs("图集尺寸和字形尺寸都未指定，使用默认值...\n", stderr);
        minEmSize = DEFAULT_SIZE;
    }
    if (config.imageType == ImageType::HARD_MASK || config.imageType == ImageType::SOFT_MASK) {
        rangeUnits = Units::PIXELS;
        rangeValue = 1;
    } else if (rangeValue.lower == rangeValue.upper) {
        rangeUnits = Units::PIXELS;
        rangeValue = DEFAULT_PIXEL_RANGE;
    }
    if (config.kerning && !(config.arteryFontFilename || config.jsonFilename || config.shadronPreviewFilename))
        config.kerning = false;
    if (config.threadCount <= 0)
        config.threadCount = std::max((int) std::thread::hardware_concurrency(), 1);
    if (config.generatorAttributes.scanlinePass) {
        if (explicitErrorCorrectionMode && config.generatorAttributes.config.errorCorrection.distanceCheckMode != msdfgen::ErrorCorrectionConfig::DO_NOT_CHECK_DISTANCE) {
            const char *fallbackModeName = "unknown";
            switch (config.generatorAttributes.config.errorCorrection.mode) {
                case msdfgen::ErrorCorrectionConfig::DISABLED: fallbackModeName = "disabled"; break;
                case msdfgen::ErrorCorrectionConfig::INDISCRIMINATE: fallbackModeName = "distance-fast"; break;
                case msdfgen::ErrorCorrectionConfig::EDGE_PRIORITY: fallbackModeName = "auto-fast"; break;
                case msdfgen::ErrorCorrectionConfig::EDGE_ONLY: fallbackModeName = "edge-fast"; break;
            }
            fprintf(stderr, "选择的错误修正模式与扫描线模式不兼容，回退到 %s。\n", fallbackModeName);
        }
        config.generatorAttributes.config.errorCorrection.distanceCheckMode = msdfgen::ErrorCorrectionConfig::DO_NOT_CHECK_DISTANCE;
    }

    // Finalize image format // 完成图像格式
    ImageFormat imageExtension = ImageFormat::UNSPECIFIED;
    if (config.imageFilename) {
        if (cmpExtension(config.imageFilename, ".png")) {
            #ifndef MSDFGEN_DISABLE_PNG
                imageExtension = ImageFormat::PNG;
            #else
                fputs("警告：您使用的此程序版本不支持 PNG 图像！\n", stderr);
            #endif
        } else if (cmpExtension(config.imageFilename, ".bmp")) imageExtension = ImageFormat::BMP;
        else if (cmpExtension(config.imageFilename, ".tiff") || cmpExtension(config.imageFilename, ".tif")) imageExtension = ImageFormat::TIFF;
        else if (cmpExtension(config.imageFilename, ".rgba")) imageExtension = ImageFormat::RGBA;
        else if (cmpExtension(config.imageFilename, ".fl32")) imageExtension = ImageFormat::FL32;
        else if (cmpExtension(config.imageFilename, ".txt")) imageExtension = ImageFormat::TEXT;
        else if (cmpExtension(config.imageFilename, ".bin")) imageExtension = ImageFormat::BINARY;
    }
    if (config.imageFormat == ImageFormat::UNSPECIFIED) {
        #ifndef MSDFGEN_DISABLE_PNG
            config.imageFormat = ImageFormat::PNG;
            imageFormatName = "png";
        #else
            config.imageFormat = ImageFormat::TIFF;
            imageFormatName = "tiff";
        #endif
        // If image format is not specified and -imageout is the only image output, infer format from its extension
        // 如果未指定图像格式且 -imageout 是唯一的图像输出，则从其扩展名推断格式
        if (!config.arteryFontFilename) {
            if (imageExtension != ImageFormat::UNSPECIFIED)
                config.imageFormat = imageExtension;
            else if (config.imageFilename)
                fprintf(stderr, "警告：无法从文件扩展名推断图像格式，将使用 %s。\n", imageFormatName);
        }
    }
#ifndef MSDF_ATLAS_NO_ARTERY_FONT
    if (config.arteryFontFilename && !(config.imageFormat == ImageFormat::PNG || config.imageFormat == ImageFormat::BINARY || config.imageFormat == ImageFormat::BINARY_FLOAT)) {
        config.arteryFontFilename = nullptr;
        result = 1;
        fputs("错误：无法使用指定的图像格式创建 Artery Font 文件！\n", stderr);
        // Recheck whether there is anything else to do
        // 重新检查是否还有其他事情要做
        if (!(config.arteryFontFilename || config.imageFilename || config.jsonFilename || config.csvFilename || config.shadronPreviewFilename))
            return result;
        layoutOnly = !(config.arteryFontFilename || config.imageFilename);
    }
#endif
    if (imageExtension != ImageFormat::UNSPECIFIED) {
        // Warn if image format mismatches -imageout extension
        // 如果图像格式与 -imageout 扩展名不匹配，则发出警告
        bool mismatch = false;
        switch (config.imageFormat) {
            case ImageFormat::TEXT: case ImageFormat::TEXT_FLOAT:
                mismatch = imageExtension != ImageFormat::TEXT;
                break;
            case ImageFormat::BINARY: case ImageFormat::BINARY_FLOAT: case ImageFormat::BINARY_FLOAT_BE:
                mismatch = imageExtension != ImageFormat::BINARY;
                break;
            default:
                mismatch = imageExtension != config.imageFormat;
        }
        if (mismatch)
            fprintf(stderr, "警告：输出图像文件扩展名与图像的实际格式（%s）不匹配！\n", imageFormatName);
    }
    imageFormatName = nullptr; // No longer consistent with imageFormat // 不再与 imageFormat 一致
    bool floatingPointFormat = (
        config.imageFormat == ImageFormat::TIFF ||
        config.imageFormat == ImageFormat::FL32 ||
        config.imageFormat == ImageFormat::TEXT_FLOAT ||
        config.imageFormat == ImageFormat::BINARY_FLOAT ||
        config.imageFormat == ImageFormat::BINARY_FLOAT_BE
    );
    // TODO: In this case (if spacing is -1), the border pixels of each glyph are black, but still computed. For floating-point output, this may play a role.
    // TODO: 在这种情况下（如果 spacing 为 -1），每个字形的边界像素是黑色的，但仍然被计算。对于浮点输出，这可能起作用。
    int spacing;
    if (config.imageType == ImageType::SDF ||config.imageType == ImageType::MSDF || config.imageType == ImageType::MTSDF) {
        spacing = 0;// 对于SDF、MSDF 和 MTSDF，默认间距为 0。
        if (packingSpacing > 0) {
            // 如果用户通过命令行明确指定了间距，则使用用户提供的值。
            spacing = packingSpacing;
        }
    } else {
        spacing = -1;// 对于其他类型（ MASK 等），默认间距为 -1（这是打包器内部的一个特殊标志，我们保留它以确保行为一致）。
    }
    double uniformOriginX, uniformOriginY;

    // Load fonts // 加载字体
    std::vector<GlyphGeometry> glyphs;
    std::vector<FontGeometry> fonts;
    bool anyCodepointsAvailable = false;
    {
        class FontHolder {
            msdfgen::FreetypeHandle *ft;
            msdfgen::FontHandle *font;
            const char *fontFilename;
        public:
            FontHolder() : ft(msdfgen::initializeFreetype()), font(nullptr), fontFilename(nullptr) { }
            ~FontHolder() {
                if (ft) {
                    if (font)
                        msdfgen::destroyFont(font);
                    msdfgen::deinitializeFreetype(ft);
                }
            }
            bool load(const char *fontFilename, bool isVarFont) {
                if (ft && fontFilename) {
                    if (this->fontFilename && !strcmp(this->fontFilename, fontFilename))
                        return true;
                    if (font)
                        msdfgen::destroyFont(font);
                    if ((font = (
                        #ifndef MSDFGEN_DISABLE_VARIABLE_FONTS
                            isVarFont ? loadVarFont(ft, fontFilename) :
                        #endif
                        msdfgen::loadFont(ft, fontFilename)
                    ))) {
                        this->fontFilename = fontFilename;
                        return true;
                    }
                    this->fontFilename = nullptr;
                }
                return false;
            }
            operator msdfgen::FontHandle *() const {
                return font;
            }
        } font;

        for (FontInput &fontInput : fontInputs) {
            if (!font.load(fontInput.fontFilename, fontInput.variableFont))
                ABORT("无法加载指定的字体文件。");
            if (fontInput.fontScale <= 0)
                fontInput.fontScale = 1;

            // Load character set  // 加载字符集
            Charset charset;
            unsigned allGlyphCount = 0;
            if (fontInput.charsetFilename) {
                if (!charset.load(fontInput.charsetFilename, fontInput.glyphIdentifierType != GlyphIdentifierType::UNICODE_CODEPOINT))
                    ABORT(fontInput.glyphIdentifierType == GlyphIdentifierType::GLYPH_INDEX ? "无法加载字形集规范。" : "无法加载字符集规范。");
            } else if (fontInput.charsetString) {
                if (!charset.parse(fontInput.charsetString, strlen(fontInput.charsetString), fontInput.glyphIdentifierType != GlyphIdentifierType::UNICODE_CODEPOINT))
                    ABORT(fontInput.glyphIdentifierType == GlyphIdentifierType::GLYPH_INDEX ? "无法解析字形集规范。" : "无法解析字符集规范。");
            } else if (fontInput.glyphIdentifierType == GlyphIdentifierType::GLYPH_INDEX)
                msdfgen::getGlyphCount(allGlyphCount, font);
            else
                charset = Charset::ASCII;

            // Load glyphs // 加载字形
            FontGeometry fontGeometry(&glyphs);
            int glyphsLoaded = -1;
            switch (fontInput.glyphIdentifierType) {
                case GlyphIdentifierType::GLYPH_INDEX:
                    if (allGlyphCount)
                        glyphsLoaded = fontGeometry.loadGlyphRange(font, fontInput.fontScale, 0, allGlyphCount, config.preprocessGeometry, config.kerning);
                    else
                        glyphsLoaded = fontGeometry.loadGlyphset(font, fontInput.fontScale, charset, config.preprocessGeometry, config.kerning);
                    break;
                case GlyphIdentifierType::UNICODE_CODEPOINT:
                    glyphsLoaded = fontGeometry.loadCharset(font, fontInput.fontScale, charset, config.preprocessGeometry, config.kerning);
                    anyCodepointsAvailable |= glyphsLoaded > 0;
                    break;
            }
            if (glyphsLoaded < 0)
                ABORT("无法从字体加载字形。");
            printf("已加载 %d 个字形中的 %d 个的几何信息", glyphsLoaded, (int) (allGlyphCount+charset.size()));
            if (fontInputs.size() > 1)
                printf("（来自字体 \"%s\"）", fontInput.fontFilename);
            printf(".\n");
            // List missing glyphs // 列出缺失的字形
            if (glyphsLoaded < (int) charset.size()) {
                fprintf(stderr, "缺失 %d 个%s", (int) charset.size()-glyphsLoaded, fontInput.glyphIdentifierType == GlyphIdentifierType::UNICODE_CODEPOINT ? "码位" : "字形");
                bool first = true;
                switch (fontInput.glyphIdentifierType) {
                    case GlyphIdentifierType::GLYPH_INDEX:
                        for (unicode_t cp : charset)
                            if (!fontGeometry.getGlyph(msdfgen::GlyphIndex(cp)))
                                fprintf(stderr, "%c 0x%02X", first ? ((first = false), ':') : ',', cp);
                        break;
                    case GlyphIdentifierType::UNICODE_CODEPOINT:
                        for (unicode_t cp : charset)
                            if (!fontGeometry.getGlyph(cp))
                                fprintf(stderr, "%c 0x%02X", first ? ((first = false), ':') : ',', cp);
                        break;
                }
                fprintf(stderr, "\n");
            } else if (glyphsLoaded < (int) allGlyphCount) {
                fprintf(stderr, "缺失 %d 个字形", (int) allGlyphCount-glyphsLoaded);
                bool first = true;
                for (unsigned i = 0; i < allGlyphCount; ++i)
                    if (!fontGeometry.getGlyph(msdfgen::GlyphIndex(i)))
                        fprintf(stderr, "%c 0x%02X", first ? ((first = false), ':') : ',', i);
                fprintf(stderr, "\n");
            }

            if (fontInput.fontName)
                fontGeometry.setName(fontInput.fontName);

            fonts.push_back((FontGeometry &&) fontGeometry);
        }
    }
    if (glyphs.empty())
        ABORT("未加载任何字形。");

    // Determine final atlas dimensions, scale and range, pack glyphs
    // 确定最终的图集尺寸、缩放和范围，打包字形
    {
        msdfgen::Range emRange = 0, pxRange = 0;
        switch (rangeUnits) {
            case Units::EMS:
                emRange = rangeValue;
                break;
            case Units::PIXELS:
                pxRange = rangeValue;
                break;
        }
        Padding innerEmPadding, outerEmPadding;
        Padding innerPxPadding, outerPxPadding;
        switch (innerPaddingUnits) {
            case Units::EMS:
                innerEmPadding = innerPadding;
                break;
            case Units::PIXELS:
                innerPxPadding = innerPadding;
                break;
        }
        switch (outerPaddingUnits) {
            case Units::EMS:
                outerEmPadding = outerPadding;
                break;
            case Units::PIXELS:
                outerPxPadding = outerPadding;
                break;
        }
        bool fixedDimensions = fixedWidth >= 0 && fixedHeight >= 0;
        bool fixedScale = config.emSize > 0;
        switch (packingStyle) {

            case PackingStyle::TIGHT: {
                TightAtlasPacker atlasPacker;
                if (fixedDimensions)
                    atlasPacker.setDimensions(fixedWidth, fixedHeight);
                else
                    atlasPacker.setDimensionsConstraint(atlasSizeConstraint);
                atlasPacker.setSpacing(spacing);
                if (fixedScale)
                    atlasPacker.setScale(config.emSize);
                else
                    atlasPacker.setMinimumScale(minEmSize);
                atlasPacker.setPixelRange(pxRange);
                atlasPacker.setUnitRange(emRange);
                atlasPacker.setMiterLimit(config.miterLimit);
                atlasPacker.setOriginPixelAlignment(config.pxAlignOriginX, config.pxAlignOriginY);
                atlasPacker.setInnerUnitPadding(innerEmPadding);
                atlasPacker.setOuterUnitPadding(outerEmPadding);
                atlasPacker.setInnerPixelPadding(innerPxPadding);
                atlasPacker.setOuterPixelPadding(outerPxPadding);
                if (int remaining = atlasPacker.pack(glyphs.data(), glyphs.size())) {
                    if (remaining < 0) {
                        ABORT("无法将字形打包到图集中。");
                    } else {
                        fprintf(stderr, "错误：无法将 %d 个字形（共 %d 个）放入图集。\n", remaining, (int) glyphs.size());
                        return 1;
                    }
                }
                atlasPacker.getDimensions(config.width, config.height);
                if (!(config.width > 0 && config.height > 0))
                    ABORT("无法确定图集尺寸。");
                config.emSize = atlasPacker.getScale();
                config.pxRange = atlasPacker.getPixelRange();
                if (!fixedScale)
                    printf("字形尺寸：%.9g 像素/em\n", config.emSize);
                if (!fixedDimensions)
                    printf("图集尺寸：%d x %d\n", config.width, config.height);
                break;
            }

            case PackingStyle::GRID: {
                GridAtlasPacker atlasPacker;
                atlasPacker.setFixedOrigin(config.grid.fixedOriginX, config.grid.fixedOriginY);
                if (fixedCellWidth >= 0 && fixedCellHeight >= 0)
                    atlasPacker.setCellDimensions(fixedCellWidth, fixedCellHeight);
                else
                    atlasPacker.setCellDimensionsConstraint(cellSizeConstraint);
                if (config.grid.cols > 0)
                    atlasPacker.setColumns(config.grid.cols);
                if (fixedDimensions)
                    atlasPacker.setDimensions(fixedWidth, fixedHeight);
                else
                    atlasPacker.setDimensionsConstraint(atlasSizeConstraint);
                atlasPacker.setSpacing(spacing);
                if (fixedScale)
                    atlasPacker.setScale(config.emSize);
                else
                    atlasPacker.setMinimumScale(minEmSize);
                atlasPacker.setPixelRange(pxRange);
                atlasPacker.setUnitRange(emRange);
                atlasPacker.setMiterLimit(config.miterLimit);
                atlasPacker.setOriginPixelAlignment(config.pxAlignOriginX, config.pxAlignOriginY);
                atlasPacker.setInnerUnitPadding(innerEmPadding);
                atlasPacker.setOuterUnitPadding(outerEmPadding);
                atlasPacker.setInnerPixelPadding(innerPxPadding);
                atlasPacker.setOuterPixelPadding(outerPxPadding);
                if (int remaining = atlasPacker.pack(glyphs.data(), glyphs.size())) {
                    if (remaining < 0) {
                        ABORT("无法将字形打包到图集中。");
                    } else {
                        fprintf(stderr, "错误：无法将 %d 个字形（共 %d 个）放入图集。\n", remaining, (int) glyphs.size());
                        return 1;
                    }
                }
                if (atlasPacker.hasCutoff())
                    fputs("警告：网格单元约束过紧，无法完全容纳所有字形，某些字形可能被截断！\n", stderr);
                atlasPacker.getDimensions(config.width, config.height);
                if (!(config.width > 0 && config.height > 0))
                    ABORT("无法确定图集尺寸。");
                config.emSize = atlasPacker.getScale();
                config.pxRange = atlasPacker.getPixelRange();
                atlasPacker.getCellDimensions(config.grid.cellWidth, config.grid.cellHeight);
                config.grid.cols = atlasPacker.getColumns();
                config.grid.rows = atlasPacker.getRows();
                if (!fixedScale)
                    printf("字形尺寸：%.9g 像素/em\n", config.emSize);
                if (config.grid.fixedOriginX || config.grid.fixedOriginY) {
                    atlasPacker.getFixedOrigin(uniformOriginX, uniformOriginY);
                    printf("网格单元原点：");
                    if (config.grid.fixedOriginX)
                        printf("X = %.9g", uniformOriginX);
                    if (config.grid.fixedOriginX && config.grid.fixedOriginY)
                        printf(", ");
                    if (config.grid.fixedOriginY) {
                        switch (config.yDirection) {
                            case YDirection::BOTTOM_UP:
                                printf("Y = %.9g", uniformOriginY);
                                break;
                            case YDirection::TOP_DOWN:
                                printf("Y = %.9g", (config.grid.cellHeight-spacing-1)/config.emSize-uniformOriginY);
                                break;
                        }
                    }
                    printf("\n");
                }
                printf("网格单元尺寸：%d x %d\n", config.grid.cellWidth, config.grid.cellHeight);
                printf("图集尺寸：%d x %d (%d 列 x %d 行)\n", config.width, config.height, config.grid.cols, config.grid.rows);
                break;
            }

        }
    }

    // Generate atlas bitmap  // 生成图集位图
    if (!layoutOnly) {

        // Edge coloring // 边缘着色
        if (config.imageType == ImageType::MSDF || config.imageType == ImageType::MTSDF) {
            if (config.expensiveColoring) {
                Workload([&glyphs, &config](int i, int threadNo) -> bool {
                    unsigned long long glyphSeed = (LCG_MULTIPLIER*(config.coloringSeed^i)+LCG_INCREMENT)*!!config.coloringSeed;
                    glyphs[i].edgeColoring(config.edgeColoring, config.angleThreshold, glyphSeed);
                    return true;
                }, glyphs.size()).finish(config.threadCount);
            } else {
                unsigned long long glyphSeed = config.coloringSeed;
                for (GlyphGeometry &glyph : glyphs) {
                    glyphSeed *= LCG_MULTIPLIER;
                    glyph.edgeColoring(config.edgeColoring, config.angleThreshold, glyphSeed);
                }
            }
        }

        bool success = false;
        switch (config.imageType) {
            case ImageType::HARD_MASK:
                if (floatingPointFormat)
                    success = makeAtlas<float, float, 1, scanlineGenerator>(glyphs, fonts, config);
                else
                    success = makeAtlas<byte, float, 1, scanlineGenerator>(glyphs, fonts, config);
                break;
            case ImageType::SOFT_MASK:
            case ImageType::SDF:
                if (floatingPointFormat)
                    success = makeAtlas<float, float, 1, sdfGenerator>(glyphs, fonts, config);
                else
                    success = makeAtlas<byte, float, 1, sdfGenerator>(glyphs, fonts, config);
                break;
            case ImageType::PSDF:
                if (floatingPointFormat)
                    success = makeAtlas<float, float, 1, psdfGenerator>(glyphs, fonts, config);
                else
                    success = makeAtlas<byte, float, 1, psdfGenerator>(glyphs, fonts, config);
                break;
            case ImageType::MSDF:
                if (floatingPointFormat)
                    success = makeAtlas<float, float, 3, msdfGenerator>(glyphs, fonts, config);
                else
                    success = makeAtlas<byte, float, 3, msdfGenerator>(glyphs, fonts, config);
                break;
            case ImageType::MTSDF:
                if (floatingPointFormat)
                    success = makeAtlas<float, float, 4, mtsdfGenerator>(glyphs, fonts, config);
                else
                    success = makeAtlas<byte, float, 4, mtsdfGenerator>(glyphs, fonts, config);
                break;
        }
        if (!success)
            result = 1;
    }

    if (config.csvFilename) {
        if (exportCSV(fonts.data(), fonts.size(), config.width, config.height, config.yDirection, config.csvFilename))
            fputs("字形布局已写入 CSV 文件。\n", stderr);
        else {
            result = 1;
            fputs("无法写入 CSV 输出文件。\n", stderr);
        }
    }

    if (config.jsonFilename) {
        JsonAtlasMetrics jsonMetrics = { };
        JsonAtlasMetrics::GridMetrics gridMetrics = { };
        jsonMetrics.distanceRange = config.pxRange;
        jsonMetrics.size = config.emSize;
        jsonMetrics.width = config.width, jsonMetrics.height = config.height;
        jsonMetrics.yDirection = config.yDirection;
        if (packingStyle == PackingStyle::GRID) {
            gridMetrics.cellWidth = config.grid.cellWidth, gridMetrics.cellHeight = config.grid.cellHeight;
            gridMetrics.columns = config.grid.cols, gridMetrics.rows = config.grid.rows;
            if (config.grid.fixedOriginX)
                gridMetrics.originX = &uniformOriginX;
            if (config.grid.fixedOriginY)
                gridMetrics.originY = &uniformOriginY;
            gridMetrics.spacing = spacing;
            jsonMetrics.grid = &gridMetrics;
        }
        if (exportJSON(fonts.data(), fonts.size(), config.imageType, jsonMetrics, config.jsonFilename, config.kerning))
            fputs("字形布局和元数据已写入 JSON 文件。\n", stderr);
        else {
            result = 1;
            fputs("无法写入 JSON 输出文件。\n", stderr);
        }
    }

    if (config.shadronPreviewFilename && config.shadronPreviewText) {
        if (anyCodepointsAvailable) {
            std::vector<unicode_t> previewText;
            utf8Decode(previewText, config.shadronPreviewText);
            previewText.push_back(0);
            if (generateShadronPreview(fonts.data(), fonts.size(), config.imageType, config.width, config.height, config.pxRange, previewText.data(), config.imageFilename, floatingPointFormat, config.shadronPreviewFilename))
                fputs("Shadron 预览脚本已生成。\n", stderr);
            else {
                result = 1;
                fputs("无法生成 Shadron 预览文件。\n", stderr);
            }
        } else {
            result = 1;
            fputs("字形集模式下不支持 Shadron 预览。\n", stderr);
        }
    }

    return result;
}

#endif
