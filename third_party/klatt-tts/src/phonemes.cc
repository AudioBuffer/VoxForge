#include "phonemes.h"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <utility>

namespace tts {

namespace {

// Default bandwidths (Hz).
constexpr float kBv1 = 60.0f, kBv2 = 90.0f, kBv3 = 150.0f;  // vowels
// Nasals: F2/F3 heavily damped so the murmur is dominated by the low nasal
// formant (otherwise the formant structure reads as a lateral or a vowel).
// Tuned so the murmur is dominated by the low nasal formant; b2 is per-place.
constexpr float kBn1 = 120.0f, kBn3 = 300.0f;                 // nasals
constexpr float kBc1 = 100.0f, kBc2 = 150.0f, kBc3 = 220.0f;  // consonants

// The master phone table. See phonemes.h for the field meanings.
// clang-format off
const std::array<Phone, 56> kPhones = {{
  // --- Vowels ---------------------------------------------------------------
  // ipa  cls                    src                  f1    f2    f3    b1    b2    b3   dur   fnp  fnz  fricCf  av   af   ah
  {"i",  PhoneClass::kVowel,     Source::kVoiced,    270, 2290, 3010, kBv1, kBv2, kBv3, 130,   0,   0,    0,  1.0f, 0,   0},
  {"\u026A", PhoneClass::kVowel, Source::kVoiced,    383, 2140, 2550, kBv1, kBv2, kBv3,  90,   0,   0,    0,  1.0f, 0,   0}, // ɪ
  {"e",  PhoneClass::kVowel,     Source::kVoiced,    460, 1990, 2530, kBv1, kBv2, kBv3, 120,   0,   0,    0,  1.0f, 0,   0},
  {"\u025B", PhoneClass::kVowel, Source::kVoiced,    528, 1784, 2480, kBv1, kBv2, kBv3, 110,   0,   0,    0,  1.0f, 0,   0}, // ɛ
  {"\u00E6", PhoneClass::kVowel, Source::kVoiced,    722, 1822, 2410, kBv1, kBv2, kBv3, 150,   0,   0,    0,  1.0f, 0,   0}, // æ
  {"\u0251", PhoneClass::kVowel, Source::kVoiced,    747, 994, 2440, kBv1, kBv2, kBv3, 150,   0,   0,    0,  1.0f, 0,   0}, // ɑ
  {"\u0254", PhoneClass::kVowel, Source::kVoiced,    482,  834, 2410, kBv1, kBv2, kBv3, 140,   0,   0,    0,  1.0f, 0,   0}, // ɔ
  {"o",  PhoneClass::kVowel,     Source::kVoiced,    450,  900, 2300, kBv1, kBv2, kBv3, 120,   0,   0,    0,  1.0f, 0,   0},
  {"\u028A", PhoneClass::kVowel, Source::kVoiced,    440, 1020, 2240, kBv1, kBv2, kBv3,  90,   0,   0,    0,  1.0f, 0,   0}, // ʊ
  {"u",  PhoneClass::kVowel,     Source::kVoiced,    300,  870, 2240, kBv1, kBv2, kBv3, 130,   0,   0,    0,  1.0f, 0,   0},
  {"\u028C", PhoneClass::kVowel, Source::kVoiced,    582, 1247, 2390, kBv1, kBv2, kBv3, 110,   0,   0,    0,  1.0f, 0,   0}, // ʌ
  {"\u025D", PhoneClass::kVowel, Source::kVoiced,    490, 1350, 1690, kBv1, kBv2, kBv3, 150,   0,   0,    0,  1.0f, 0,   0}, // ɝ
  {"\u0259", PhoneClass::kVowel, Source::kVoiced,    426, 1498, 2500, kBv1, kBv2, kBv3,  70,   0,   0,    0,  1.0f, 0,   0}, // ə

  // --- Stops (loci + burst centre; expanded into closure/burst at synth) -----
  {"p",  PhoneClass::kStop,      Source::kVoiceless, 300,  720, 2200, kBc1, kBc2, kBc3,  90,   0,   0, 1200, 0.0f, 0.5f, 0.4f},
  {"b",  PhoneClass::kStop,      Source::kVoiced,    300,  720, 2200, kBc1, kBc2, kBc3,  80,   0,   0, 1200, 0.4f, 0.4f, 0.0f},
  {"t",  PhoneClass::kStop,      Source::kVoiceless, 300, 1750, 2600, kBc1, kBc2, kBc3,  90,   0,   0, 3800, 0.0f, 0.6f, 0.4f},
  {"d",  PhoneClass::kStop,      Source::kVoiced,    300, 1750, 2600, kBc1, kBc2, kBc3,  80,   0,   0, 3800, 0.4f, 0.5f, 0.0f},
  {"k",  PhoneClass::kStop,      Source::kVoiceless, 300, 1900, 2400, kBc1, kBc2, kBc3,  90,   0,   0, 2200, 0.0f, 0.5f, 0.5f},
  {"g",  PhoneClass::kStop,      Source::kVoiced,    300, 1900, 2400, kBc1, kBc2, kBc3,  80,   0,   0, 2200, 0.4f, 0.45f,0.0f},

  // --- Nasals ---------------------------------------------------------------
  // fnz is the place-dependent oral-cavity anti-resonance (the nasal place cue):
  // low for labial /m/, mid for alveolar /n/, high for velar /ŋ/.
  {"m",  PhoneClass::kNasal,     Source::kVoiced,    220, 1000, 2200, kBn1, 330, kBn3,  80, 250, 1033,  0,  1.0f, 0,   0},
  {"n",  PhoneClass::kNasal,     Source::kVoiced,    220, 1600, 2700, kBn1, 197, kBn3,  80, 250, 1308,  0,  1.0f, 0,   0},
  {"\u014B", PhoneClass::kNasal, Source::kVoiced,    220, 2000, 2600, kBn1, 259, kBn3,  80, 250, 2415,  0,  1.0f, 0,   0}, // ŋ

  // --- Fricatives -----------------------------------------------------------
  {"f",  PhoneClass::kFricative, Source::kVoiceless, 300, 1100, 2200, kBc1, kBc2, kBc3, 110,   0,   0, 1827, 0.0f, 0.18f,0},
  {"v",  PhoneClass::kFricative, Source::kMixed,     300, 1100, 2200, kBc1, kBc2, kBc3,  80,   0,   0, 1827, 0.25f,0.16f,0},
  {"\u03B8", PhoneClass::kFricative, Source::kVoiceless, 300, 1400, 2400, kBc1, kBc2, kBc3, 100, 0, 0, 2770, 0.0f, 0.16f,0}, // θ
  {"\u00F0", PhoneClass::kFricative, Source::kMixed, 300, 1400, 2400, kBc1, kBc2, kBc3,  70,   0,   0, 2770, 0.25f,0.14f,0}, // ð
  {"s",  PhoneClass::kFricative, Source::kVoiceless, 300, 1700, 2600, kBc1, kBc2, kBc3, 120,   0,   0, 5344, 0.0f, 0.7f, 0},
  {"z",  PhoneClass::kFricative, Source::kMixed,     300, 1700, 2600, kBc1, kBc2, kBc3,  90,   0,   0, 5344, 0.3f, 0.5f, 0},
  {"\u0283", PhoneClass::kFricative, Source::kVoiceless, 300, 1800, 2500, kBc1, kBc2, kBc3, 120, 0, 0, 2939, 0.0f, 0.75f,0}, // ʃ
  {"\u0292", PhoneClass::kFricative, Source::kMixed, 300, 1800, 2500, kBc1, kBc2, kBc3,  90,   0,   0, 2939, 0.3f, 0.55f,0}, // ʒ
  {"h",  PhoneClass::kFricative, Source::kVoiceless, 500, 1500, 2500, kBc1, kBc2, kBc3,  70,   0,   0,    0, 0.0f, 0.0f, 0.5f},

  // --- Approximants & lateral ----------------------------------------------
  {"\u0279", PhoneClass::kApproximant, Source::kVoiced, 330, 1100, 1600, kBv1, kBv2, kBv3, 80, 0, 0, 0, 1.0f, 0, 0}, // ɹ
  {"j",  PhoneClass::kApproximant, Source::kVoiced,    250, 2300, 3000, kBv1, kBv2, kBv3,  70,   0,   0,    0,  1.0f, 0, 0},
  {"w",  PhoneClass::kApproximant, Source::kVoiced,    290,  610, 2150, kBv1, kBv2, kBv3,  80,   0,   0,    0,  1.0f, 0, 0},
  {"l",  PhoneClass::kLateral,     Source::kVoiced,    360, 1300, 2700, kBv1, kBv2, kBv3,  80,   0,   0,    0,  1.0f, 0, 0},

  // --- Silence (used for pauses) -------------------------------------------
  {" ",  PhoneClass::kSilence,  Source::kSilence,   500, 1500, 2500, kBv1, kBv2, kBv3,  60,   0,   0,    0,  0.0f, 0, 0},
  {".",  PhoneClass::kSilence,  Source::kSilence,   500, 1500, 2500, kBv1, kBv2, kBv3, 220,   0,   0,    0,  0.0f, 0, 0},

  // --- 汉语扩展（普通话里英语表没有的音）-------------------------------------
  // 舌尖元音 [ɨ]：zhi/chi/shi/ri/zi/ci/si 的韵母
  {"\u0268", PhoneClass::kVowel,     Source::kVoiced,   350, 1400, 2200, kBv1, kBv2, kBv3, 130,   0,   0,    0,  1.0f, 0, 0},
  // 前圆唇元音 [y]：ü
  {"y",  PhoneClass::kVowel,     Source::kVoiced,   300, 1900, 2300, kBv1, kBv2, kBv3, 130,   0,   0,    0,  1.0f, 0, 0},
  // 卷舌擦音 [ʂ]：sh
  {"\u0282", PhoneClass::kFricative, Source::kVoiceless, 300, 1600, 2100, kBc1, kBc2, kBc3, 150, 0, 0, 2700, 0.0f, 0.92f, 0},
  // 舌面擦音 [ɕ]：x
  {"\u0255", PhoneClass::kFricative, Source::kVoiceless, 300, 2000, 2800, kBc1, kBc2, kBc3, 140, 0, 0, 4200, 0.0f, 0.80f, 0},
  // 卷舌近音 [ɻ]：r
  {"\u027B", PhoneClass::kApproximant, Source::kVoiced, 330, 1250, 1700, kBv1, kBv2, kBv3, 80, 0, 0, 0, 1.0f, 0, 0},
  // 卷舌塞擦音 [ʈʂ]：zh
  {"\u0288\u0282", PhoneClass::kStop, Source::kVoiceless, 300, 1600, 2100, kBc1, kBc2, kBc3, 90, 0, 0, 2900, 0.0f, 0.6f, 0.35f},
  // 舌面塞擦音 [tɕ]：j
  {"t\u0255", PhoneClass::kStop, Source::kVoiceless, 300, 2000, 2800, kBc1, kBc2, kBc3, 90, 0, 0, 4500, 0.0f, 0.55f, 0.35f},
  // --- 中文/日文补充 ---------------------------------------------------------
  {"p\u02B0", PhoneClass::kStop, Source::kVoiceless, 300,  800, 2200, kBc1, kBc2, kBc3, 90, 0, 0, 1200, 0.0f, 0.45f, 0.50f},
  {"t\u02B0", PhoneClass::kStop, Source::kVoiceless, 300, 1750, 2600, kBc1, kBc2, kBc3, 90, 0, 0, 3800, 0.0f, 0.55f, 0.50f},
  {"k\u02B0", PhoneClass::kStop, Source::kVoiceless, 300, 1900, 2400, kBc1, kBc2, kBc3, 90, 0, 0, 2200, 0.0f, 0.50f, 0.55f},
  {"ts\u02B0", PhoneClass::kStop, Source::kVoiceless, 300, 1700, 2600, kBc1, kBc2, kBc3, 100, 0, 0, 5344, 0.0f, 0.6f, 0.4f},
  {"\u0288\u0282\u02B0", PhoneClass::kStop, Source::kVoiceless, 300, 1600, 2100, kBc1, kBc2, kBc3, 100, 0, 0, 2700, 0.0f, 0.6f, 0.4f},
  {"t\u0255\u02B0", PhoneClass::kStop, Source::kVoiceless, 300, 2000, 2800, kBc1, kBc2, kBc3, 100, 0, 0, 4200, 0.0f, 0.6f, 0.4f},
  {"d\u0291", PhoneClass::kStop, Source::kVoiced, 300, 2000, 2800, kBc1, kBc2, kBc3, 90, 0, 0, 4200, 0.4f, 0.4f, 0.0f},
  {"\u0278", PhoneClass::kFricative, Source::kVoiceless, 300, 1400, 2200, kBc1, kBc2, kBc3, 110, 0, 0, 1800, 0.0f, 0.30f, 0.2f},
  {"\u00E7", PhoneClass::kFricative, Source::kVoiceless, 300, 2200, 3000, kBc1, kBc2, kBc3, 110, 0, 0, 3500, 0.0f, 0.45f, 0.15f},
  {"\u026F", PhoneClass::kVowel, Source::kVoiced, 300, 1350, 2200, kBv1, kBv2, kBv3, 120, 0, 0, 0, 1.0f, 0, 0},
  {"\u0274", PhoneClass::kNasal, Source::kVoiced, 300, 1000, 2200, 120, 250, kBn3, 80, 270, 450, 0, 1.0f, 0, 0},
  {"\u028F", PhoneClass::kVowel, Source::kVoiced, 300, 1850, 2350, kBv1, kBv2, kBv3, 130, 0, 0, 0, 1.0f, 0, 0},
}};
// clang-format on

const std::unordered_map<std::string, const Phone*>& PhoneIndex() {
  static const std::unordered_map<std::string, const Phone*> index = [] {
    std::unordered_map<std::string, const Phone*> m;
    for (const Phone& p : kPhones) {
      m.emplace(p.ipa, &p);
    }
    return m;
  }();
  return index;
}

}  // namespace

const Phone* LookupPhone(const std::string& ipa) {
  const auto& idx = PhoneIndex();
  auto it = idx.find(ipa);
  return it == idx.end() ? nullptr : it->second;
}

std::vector<Phone> DefaultPhoneTable() {
  return std::vector<Phone>(kPhones.begin(), kPhones.end());
}

}  // namespace tts
