// GENERERET af web/touch-ui/gen_problem_texts.py fra web/touch-ui/i18n/*.json — ret ikke i hånden.
// Problemtitler og -beskeder til esphome.lune_problem; {navn} udfyldes af lune_touch_problems::fill().
#pragma once

#include <cstdint>

namespace lune_touch_problem_texts {

enum Key : uint8_t {
  HP_OVER_TITLE,
  HP_OVER_MSG,
  HP_UNDER_TITLE,
  HP_UNDER_MSG,
  MIXING_TITLE,
  MIXING_MSG,
  V6_TITLE,
  V6_MSG,
  ASGARD_TITLE,
  ASGARD_MSG,
  ODIN_TITLE,
  ODIN_MSG,
  ROOM_TITLE,
  ROOM_MSG,
  SHORT_FLOW_TITLE,
  SHORT_FLOW_MSG,
  KEY_COUNT,
};

static const char *const DA[KEY_COUNT] = {
    "Varmepumpen leverer mere end planlagt",
    "Varmepumpen har i en halv time leveret {mean} kW mod Odins plan på {plan} kW.",
    "Varmepumpen leverer mindre end planlagt",
    "Varmepumpen har i en halv time leveret {mean} kW mod Odins plan på {plan} kW.",
    "Blanding i buffertanken",
    "Sekundært flow ({sec} l/min) er langt højere end primært ({pri} l/min), så returvand blandes i fremløbet. Sænk cirkulationspumpen til ca. 5 % over varmepumpens flow.",
    "{name} svarer ikke",
    "Touch har ikke haft forbindelse til {name} i over 10 minutter. Rummene på styringen kører videre lokalt.",
    "Asgard svarer ikke",
    "Touch har ikke haft forbindelse til Asgard i over 10 minutter. Hustemperaturen når ikke frem til varmepumpen.",
    "Odin svarer ikke",
    "Touch har ikke haft forbindelse til Odin i over 30 minutter. Varmeplanen bliver ikke opdateret.",
    "{room} når ikke sin temperatur",
    "{room} har ikke nået {target} °C i 24 timer (højst {max} °C).",
    "For lidt flow på sekundærsiden",
    "Sekundært flow ({sec} l/min) er lavere end primært ({pri} l/min), så varmepumpens overskud løber gennem buffertanken tilbage til den, og den kører korte drifter. Hæv cirkulationspumpen til ca. 5 % over varmepumpens flow.",
};
static constexpr char DA_DEC = ',';

static const char *const EN[KEY_COUNT] = {
    "Heat pump delivers more than planned",
    "For half an hour the heat pump has delivered {mean} kW against Odin's plan of {plan} kW.",
    "Heat pump delivers less than planned",
    "For half an hour the heat pump has delivered {mean} kW against Odin's plan of {plan} kW.",
    "Mixing in the buffer tank",
    "Secondary flow ({sec} l/min) is far above primary ({pri} l/min), so floor return mixes into the supply. Lower the circulation pump to about 5 % above the heat pump's flow.",
    "{name} is not answering",
    "Touch has had no contact with {name} for more than 10 minutes. Its rooms keep running locally.",
    "Asgard is not answering",
    "Touch has had no contact with Asgard for more than 10 minutes. The house temperature does not reach the heat pump.",
    "Odin is not answering",
    "Touch has had no contact with Odin for more than 30 minutes. The heating plan is not updated.",
    "{room} is not reaching its temperature",
    "{room} has not reached {target} °C in 24 hours (highest {max} °C).",
    "Too little flow on the secondary side",
    "Secondary flow ({sec} l/min) is below primary ({pri} l/min), so the heat pump's surplus returns to it through the buffer tank and it runs in short cycles. Raise the circulation pump to about 5 % above the heat pump's flow.",
};
static constexpr char EN_DEC = '.';

/// Catalogue for a language code ("da" | "en"); Danish is the default.
inline const char *const *catalogue(const char *lang) {
  return (lang != nullptr && lang[0] == 'e' && lang[1] == 'n') ? EN : DA;
}

inline char decimal_sign(const char *lang) {
  return (lang != nullptr && lang[0] == 'e' && lang[1] == 'n') ? EN_DEC : DA_DEC;
}

}  // namespace lune_touch_problem_texts
