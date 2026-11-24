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
  MIXING_MSG_LOW,
  FLOW_LOW_TITLE,
  FLOW_LOW_MSG,
  FLOW_HIGH_TITLE,
  FLOW_HIGH_MSG,
  V6_TITLE,
  V6_MSG,
  ASGARD_TITLE,
  ASGARD_MSG,
  ODIN_TITLE,
  ODIN_MSG,
  ROOM_TITLE,
  ROOM_MSG,
  KEY_COUNT,
};

static const char *const DA[KEY_COUNT] = {
    "Varmepumpen leverer mere end planlagt",
    "Varmepumpen har i en halv time leveret {mean} kW mod Odins plan på {plan} kW.",
    "Varmepumpen leverer mindre end planlagt",
    "Varmepumpen har i en halv time leveret {mean} kW mod Odins plan på {plan} kW.",
    "Blanding i buffertanken",
    "Sekundært flow ({sec} l/min) er over primært ({pri} l/min), fordi mange kredse er åbne. Returvand blandes i fremløbet, så varmepumpen kører varmere.",
    "Sekundært flow ({sec} l/min) er under primært ({pri} l/min), fordi få kredse er åbne. Overskuddet løber gennem tanken tilbage og hæver varmepumpens retur.",
    "For lidt flow til de åbne kredse",
    "Pumpen flytter {sec} l/min, men de åbne kredse ({open} m²) plejer at tage ca. {exp} l/min. Tjek filteret, luft i kredsene og pumpen.",
    "Mere flow end de åbne kredse forklarer",
    "Pumpen flytter {sec} l/min, men de åbne kredse ({open} m²) plejer at tage ca. {exp} l/min. En ventil lukker måske ikke, eller pumpen er indstillet om.",
    "{name} svarer ikke",
    "Touch har ikke haft forbindelse til {name} i over 10 minutter. Rummene på styringen kører videre lokalt.",
    "Asgard svarer ikke",
    "Touch har ikke haft forbindelse til Asgard i over 10 minutter. Hustemperaturen når ikke frem til varmepumpen.",
    "Odin svarer ikke",
    "Touch har ikke haft forbindelse til Odin i over 30 minutter. Varmeplanen bliver ikke opdateret.",
    "{room} når ikke sin temperatur",
    "{room} har ikke nået {target} °C i 24 timer (højst {max} °C).",
};
static constexpr char DA_DEC = ',';

static const char *const EN[KEY_COUNT] = {
    "Heat pump delivers more than planned",
    "For half an hour the heat pump has delivered {mean} kW against Odin's plan of {plan} kW.",
    "Heat pump delivers less than planned",
    "For half an hour the heat pump has delivered {mean} kW against Odin's plan of {plan} kW.",
    "Mixing in the buffer tank",
    "Secondary flow ({sec} l/min) is above primary ({pri} l/min) because many loops are open. Floor return mixes into the supply, so the heat pump runs hotter.",
    "Secondary flow ({sec} l/min) is below primary ({pri} l/min) because few loops are open. The surplus runs back through the tank and warms the heat pump's return.",
    "Too little flow for the open loops",
    "The pump moves {sec} l/min, but the open loops ({open} m²) usually take about {exp} l/min. Check the filter, air in the loops and the pump.",
    "More flow than the open loops explain",
    "The pump moves {sec} l/min, but the open loops ({open} m²) usually take about {exp} l/min. A valve may not close, or the pump setting changed.",
    "{name} is not answering",
    "Touch has had no contact with {name} for more than 10 minutes. Its rooms keep running locally.",
    "Asgard is not answering",
    "Touch has had no contact with Asgard for more than 10 minutes. The house temperature does not reach the heat pump.",
    "Odin is not answering",
    "Touch has had no contact with Odin for more than 30 minutes. The heating plan is not updated.",
    "{room} is not reaching its temperature",
    "{room} has not reached {target} °C in 24 hours (highest {max} °C).",
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
