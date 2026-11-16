# Shadow planner (pure C++ headers): history format, house and heat-pump models, solver.
# No YAML config of its own; lune_touch_coordinator auto-loads it so the headers are
# built into the firmware. Host tests: make test-planner.
import esphome.config_validation as cv

CODEOWNERS = []
CONFIG_SCHEMA = cv.Schema({})


async def to_code(config):
    pass
