#pragma once
#include "anomaly/sdk/base.h"

#define ANOMALY_JSON_SERVICE_V1_ID "anomaly.json"
#define ANOMALY_JSON_SERVICE_V1_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AnomalyJsonKindV1 {
    ANOMALY_JSON_V1_NULL = 0,
    ANOMALY_JSON_V1_BOOLEAN = 1,
    ANOMALY_JSON_V1_NUMBER = 2,
    ANOMALY_JSON_V1_STRING = 3,
    ANOMALY_JSON_V1_ARRAY = 4,
    ANOMALY_JSON_V1_OBJECT = 5
} AnomalyJsonKindV1;

// A lightweight, plugin-scoped DOM over nlohmann/json. Every handle returned by
// parse, array_item, and object_find is owned by the plugin scope and must be
// released before the DLL is unloaded. Handle tokens are invalidated when the
// owning plugin generation stops.
typedef struct AnomalyJsonServiceV1 {
    uint32_t struct_size;
    uint32_t service_version;
    void* user;
    AnomalyStatusV1 (ANOMALY_CALL *parse)(
        void* user, AnomalyStringViewV1 document,
        AnomalyGenerationHandleV1* handle);
    AnomalyStatusV1 (ANOMALY_CALL *release)(
        void* user, AnomalyGenerationHandleV1 handle);
    AnomalyStatusV1 (ANOMALY_CALL *kind)(
        void* user, AnomalyGenerationHandleV1 handle, uint32_t* kind);
    AnomalyStatusV1 (ANOMALY_CALL *boolean_value)(
        void* user, AnomalyGenerationHandleV1 handle, int32_t* value);
    AnomalyStatusV1 (ANOMALY_CALL *number_value)(
        void* user, AnomalyGenerationHandleV1 handle, double* value);
    AnomalyStatusV1 (ANOMALY_CALL *string_value)(
        void* user, AnomalyGenerationHandleV1 handle,
        char* destination, size_t* inout_size);
    AnomalyStatusV1 (ANOMALY_CALL *array_size)(
        void* user, AnomalyGenerationHandleV1 handle, size_t* size);
    AnomalyStatusV1 (ANOMALY_CALL *array_item)(
        void* user, AnomalyGenerationHandleV1 handle, size_t index,
        AnomalyGenerationHandleV1* child);
    AnomalyStatusV1 (ANOMALY_CALL *object_size)(
        void* user, AnomalyGenerationHandleV1 handle, size_t* size);
    AnomalyStatusV1 (ANOMALY_CALL *object_key_at)(
        void* user, AnomalyGenerationHandleV1 handle, size_t index,
        char* destination, size_t* inout_size);
    AnomalyStatusV1 (ANOMALY_CALL *object_find)(
        void* user, AnomalyGenerationHandleV1 handle, AnomalyStringViewV1 key,
        AnomalyGenerationHandleV1* child);
    AnomalyStatusV1 (ANOMALY_CALL *serialize)(
        void* user, AnomalyGenerationHandleV1 handle,
        char* destination, size_t* inout_size);
} AnomalyJsonServiceV1;

#ifdef __cplusplus
}
#endif
