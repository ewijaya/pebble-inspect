#pragma once
#include "model.h"

void inspect_storage_load(InspectModel *model);
bool inspect_storage_save(const InspectModel *model);
const char *inspect_storage_status(void);
