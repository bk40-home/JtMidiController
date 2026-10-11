// =============================================================================
// PatternPresetApply.cpp — see PatternPresetApply.h for the contract.
// =============================================================================
#include "PatternPresetApply.h"

#include "JtParamModel.h"

namespace JtPresets {

namespace PP = JT::PatternPresets;

ApplyResult apply(JtParam::Store& store, FamilyId fam, uint8_t index) {
    ApplyResult r;
    if (static_cast<uint8_t>(fam) >= static_cast<uint8_t>(FamilyId::Count)) return r;

    const PP::Family& f = PP::family(fam);
    if (index >= f.count) return r;

    const PP::Preset& p = f.presets[index];
    r.writes = p.count;

    for (uint16_t i = 0; i < p.count; ++i) {
        const PP::Write& w = p.writes[i];
        const JT::Params::ParamDesc* d = JtParam::descOf(w.id);
        if (!d) { ++r.missing; continue; }

        // toNorm is the inverse of toEng for every curve and for selects /
        // toggles / ints, so the engineering value lands on the exact t the
        // engine will decode back to it.
        if (store.setById(w.id, JtParam::toNorm(*d, w.eng))) ++r.changed;
    }
    return r;
}

} // namespace JtPresets
