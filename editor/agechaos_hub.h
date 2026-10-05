#pragma once
#include "core/error/error_list.h"
#include "core/string/ustring.h"

// The project manager and asset store live in the C# Hub application.
Error agechaos_open_hub(const String &p_project = String());
