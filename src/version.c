#include "version.h"

/* Defaults if not provided via -D */
#ifndef BEE_MANUFACTURER_NAME
#define BEE_MANUFACTURER_NAME "BeeEye"
#endif

#ifndef BEE_MODEL_ID
#define BEE_MODEL_ID "MPsensor"
#endif

const char *bee_manufacturer_name = BEE_MANUFACTURER_NAME;
const char *bee_model_id          = BEE_MODEL_ID;
