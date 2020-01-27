#ifndef HORSE3D_LIGHTS_H_
#define HORSE3D_LIGHTS_H_


#define MAXLIGHTS 8
#define MAXACTIVELIGHTS 6

#define FALLOFF_LINEAR 0
#define FALLOFF_INVERSEEXP 1

typedef struct h3dlight h3dlight;

typedef struct h3dlightsettings {
    double r, g, b;
    double range;

    int fallofftype;
} h3dlightsettings;


h3dlight *lights_New(
    const double position[3]
);

h3dlightsettings *lights_GetLightSettings(h3dlight *l);

int lights_GetLightId(h3dlight *l);

h3dlight **lights_EnableNthActiveLights(
    const double position[3], int count
);

static inline int lights_GetMaxLights() {
    return MAXLIGHTS;
}

void lights_GetPosition(h3dlight *l, double position[3]);

void lights_MoveLight(h3dlight *l, const double position[3]);

void lights_DestroyLight(h3dlight *l);

h3dlight *lights_GetLightById(int64_t id);

int lights_AddLightTag(h3dlight *l, const char *tag);

double lights_GetFadeFactor(h3dlight *l);

void lights_UpdateFade();

#endif  // HORSE3D_LIGHTS_H_
