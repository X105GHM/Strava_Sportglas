#pragma once
#include "storage/PersistentState.hpp"

class ApiClient
{
public:
    bool fetch(const DeviceCredentials &, SavedState &);
};
