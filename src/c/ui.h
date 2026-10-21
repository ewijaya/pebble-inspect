#pragma once
#include "provider.h"

typedef void (*InspectNavigationHandler)(void);
bool inspect_ui_init(const InspectModel *model, const InspectSnapshot *snapshot,
                     InspectNavigationHandler navigation_handler);
void inspect_ui_refresh(void);
void inspect_ui_deinit(void);
