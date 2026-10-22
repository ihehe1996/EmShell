#include "app/palettes/Palette.h"

#include <algorithm>

namespace Em {

namespace {

// 函数内静态：各配色文件的静态 registrar 谁先跑不确定，
// 但只要大家都在 main 之前跑完，这里第一次取用时就一定能看到全部登记项。
QVector<PaletteSpec> &registry()
{
    static QVector<PaletteSpec> specs;
    return specs;
}

} // namespace

PaletteRegistrar::PaletteRegistrar(const PaletteSpec &spec)
{
    registerPalette(spec);
}

void registerPalette(const PaletteSpec &spec)
{
    registry().append(spec);
}

const QVector<PaletteSpec> &paletteSpecs()
{
    static const QVector<PaletteSpec> sorted = [] {
        QVector<PaletteSpec> copy = registry();
        std::stable_sort(copy.begin(), copy.end(),
                         [](const PaletteSpec &a, const PaletteSpec &b) {
                             return a.order < b.order;
                         });
        return copy;
    }();
    return sorted;
}

const PaletteSpec &defaultPalette()
{
    static const PaletteSpec &fallback = []() -> const PaletteSpec & {
        const QVector<PaletteSpec> &specs = paletteSpecs();
        for (const PaletteSpec &spec : specs) {
            if (spec.isDefault)
                return spec;
        }
        static const PaletteSpec empty;
        return specs.isEmpty() ? empty : specs.first();
    }();
    return fallback;
}

const PaletteSpec *findPalette(const QString &key)
{
    for (const PaletteSpec &spec : paletteSpecs()) {
        if (spec.key == key)
            return &spec;
    }
    return nullptr;
}

} // namespace Em
