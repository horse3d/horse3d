
#include <assert.h>
#define BT_USE_DOUBLE_PRECISION
#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionDispatch/btCollisionDispatcher.h>
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
extern "C" {
#include <inttypes.h>
#include <mathc/mathc.h>
#include <SDL2/SDL.h>
}

extern "C" {
#include "colworld.h"
#include "colworld_internal.h"
#include "charactercontroller.h"
#include "../datetime.h"
#include "../math3d.h"
#include "../world.h"
}

//#define DEBUGCHARACTERPHYSICS
#define FORCES_MAX 10

typedef struct h3dcolcharacter {
    h3dcolobject *colobj;
    h3dobject *obj;
    double step_height_scalar;

    int pretend_character_is_human_sized;
    int onfloor, onslope;
    int frames_no_vertical_velocity, frames_no_velocity,
        frames_no_horizontal_velocity;

    int cached_lowheightsweeps;
    double cached_lowheightsweeps_ypos;
    double cached_lowheightsweeps_xpos;
    double cached_lowheightsweeps_yneg;
    double cached_lowheightsweeps_xneg;

    int forcescount;
    double forces[3 * FORCES_MAX];
} h3dcolcharacter;


extern "C" {

int charactercontroller_GetOnFloor(
        h3dcolcharacter *colchar
        ) {
    return colchar->onfloor;
}

int charactercontroller_GetOnSlope(
        h3dcolcharacter *colchar
        ) {
    return colchar->onslope;
}

h3dcolcharacter *charactercontroller_New(
        h3dobject *charobj,
        double step_height) {
    if (!charobj || !charobj->colobject)
        return NULL;
    h3dcolcharacter *colchar = (h3dcolcharacter *)malloc(
        sizeof(*colchar)
    );
    if (!colchar)
        return NULL;
    memset(colchar, 0, sizeof(*colchar));
    h3dcolobject *colobj = charobj->colobject;
    colchar->colobj = colobj;
    assert(step_height > 0);
    colchar->step_height_scalar = step_height;

    colworld_SetObjectFixedAngle(colobj, 1);
    colworld_DisableObjectGravity(colobj, 1);
    colchar->obj = charobj;
    double sx, sy, sz;
    world_GetObjectDimensions(
        charobj, &sx, &sy, &sz
    );
    charactercontroller_UpdateMassAfterScaling(colchar);
    return colchar;
}

void charactercontroller_UpdateMassAfterScaling(
        h3dcolcharacter *colchar
        ) {
    if (colchar->obj->automass) {
        double sx, sy, sz;
        world_GetObjectDimensions(
            colchar->obj, &sx, &sy, &sz
        );
        world_SetObjectMass(colchar->obj, 100 * sx * sy * sz);
        colchar->obj->automass = 1;
    }
}

double charactercontroller_ScaleBasedForcesFactor(
        h3dcolcharacter *charobj
        ) {
    double mass = colworld_GetObjectMass(charobj->colobj);
    double mass_f = mass;
    double avg_size_default = (1.7 + 0.6 + 0.6) / 3;
    double sx, sy, sz;
    world_GetObjectDimensions(
        charobj->obj, &sx, &sy, &sz
    );
    double avg_size_actual = (sx + sy + sz) / 3;
    double avg_size_f = (
        avg_size_actual / avg_size_default
    );
    if (charobj->pretend_character_is_human_sized)
        return (mass_f * avg_size_f * 0.7) + mass_f * 0.3;
    else
        return mass_f;
}

void charactercontroller_ApplyForce(
        h3dcolcharacter *colchar, double *force
        ) {
    if (!colchar)
        return;
    #if defined(DEBUGCHARACTERPHYSICS) && !defined(NDEBUG)
    printf(
        "horse3d/collision/charactercontroller.cpp: verbose: "
        "CT%p: ApplyForce(%p,%f,%f,%f)\n",
        colchar, colchar, force[0], force[1], force[2]
    );
    #endif
    int i = colchar->forcescount;
    if (i >= FORCES_MAX)
        i = FORCES_MAX - 1;
    else
        colchar->forcescount++;
    colchar->forces[3 * i + 0] = force[0];
    colchar->forces[3 * i + 1] = force[1];
    colchar->forces[3 * i + 2] = force[2];
}

void charactercontroller_CalculateHalfheightSweeps(
        h3dcolcharacter *colchar
        ) {
    if (colchar->cached_lowheightsweeps)
        return;

    double sx, sy, sz;
    world_GetObjectDimensions(
        colchar->obj, &sx, &sy, &sz
    );

    double scan_distance = fmin(sx, sy) * 0.5;
    double scanpos[3];
    double ourpos[3];
    colworld_GetObjectPosition(
        colchar->colobj, &ourpos[0], &ourpos[1], &ourpos[2]
    );

    scanpos[0] = ourpos[0];
    scanpos[1] = ourpos[1] + scan_distance;
    scanpos[2] = ourpos[2];
    if (!colworld_ObjectDoSweepLowHeight(
            colchar->colobj, scanpos,
            &colchar->cached_lowheightsweeps_ypos,
            NULL
            )) {
        colchar->cached_lowheightsweeps_ypos = -1;
    } else {
        if (colchar->cached_lowheightsweeps_ypos < 0)
            colchar->cached_lowheightsweeps_ypos = 0;
    }
    scanpos[0] = ourpos[0];
    scanpos[1] = ourpos[1] - scan_distance;
    scanpos[2] = ourpos[2];
    if (!colworld_ObjectDoSweepLowHeight(
            colchar->colobj, scanpos,
            &colchar->cached_lowheightsweeps_yneg,
            NULL
            )) {
        colchar->cached_lowheightsweeps_yneg = -1;
    } else {
        if (colchar->cached_lowheightsweeps_yneg < 0)
            colchar->cached_lowheightsweeps_yneg = 0;
    }
    scanpos[0] = ourpos[0] + scan_distance;
    scanpos[1] = ourpos[1];
    scanpos[2] = ourpos[2];
    if (!colworld_ObjectDoSweepLowHeight(
            colchar->colobj, scanpos,
            &colchar->cached_lowheightsweeps_xpos,
            NULL
            )) {
        colchar->cached_lowheightsweeps_xpos = -1;
    } else {
        if (colchar->cached_lowheightsweeps_xpos < 0)
            colchar->cached_lowheightsweeps_xpos = 0;
    }
    scanpos[0] = ourpos[0] - scan_distance;
    scanpos[1] = ourpos[1];
    scanpos[2] = ourpos[2];
    if (!colworld_ObjectDoSweepLowHeight(
            colchar->colobj, scanpos,
            &colchar->cached_lowheightsweeps_xneg,
            NULL
            )) {
        colchar->cached_lowheightsweeps_xneg = -1;
    } else {
        if (colchar->cached_lowheightsweeps_xneg < 0)
            colchar->cached_lowheightsweeps_xneg = 0;
    }

    colchar->cached_lowheightsweeps = 1;
}

void charactercontroller_Update(h3dcolcharacter *colchar) {
    colchar->cached_lowheightsweeps = 0;

    double mass = colworld_GetObjectMass(colchar->colobj);
    double forcescalefactor = (
        charactercontroller_ScaleBasedForcesFactor(
            colchar
        )
    );
    double forcedampbasedfactor = 40000;
    double timestep_f = (PHYSICS_STEP_MS/(1000.0f/60.0f));
    double sx, sy, sz;
    world_GetObjectDimensions(
        colchar->obj, &sx, &sy, &sz
    );
    double ourpos[3];
    colworld_GetObjectPosition(
        colchar->colobj, &ourpos[0], &ourpos[1], &ourpos[2]
    );
    double scanpos[3];
    memcpy(scanpos, ourpos, sizeof(scanpos));
    scanpos[2] -= colchar->colobj->dimensions_z * 10.0 *
        colchar->obj->scale;
    double step_height = (
        colchar->colobj->dimensions_z * colchar->step_height_scalar *
        colchar->obj->scale
    );
    double original_object_height = (
        colchar->colobj->dimensions_z * colchar->obj->scale + step_height
    );
    double hover_intended = (
        colchar->colobj->dimensions_z * 0.5 *
        colchar->obj->scale + step_height
    );
    if (fabs(colchar->colobj->rigidbody->getLinearVelocity().getZ()) <
            sz * 0.2) {
        colchar->frames_no_vertical_velocity++;
        if (fabs(colchar->colobj->rigidbody->getLinearVelocity().getX()) <
                sx * 0.2 &&
                fabs(colchar->colobj->rigidbody->getLinearVelocity().getY()) <
                sy * 0.2) {
            colchar->frames_no_velocity++;
            colchar->frames_no_horizontal_velocity++;
        } else {
            colchar->frames_no_velocity = 0;
            colchar->frames_no_horizontal_velocity = 0;
        }
    } else {
        colchar->frames_no_vertical_velocity = 0;
        colchar->frames_no_velocity = 0;
        if (fabs(colchar->colobj->rigidbody->getLinearVelocity().getX()) <
                sx * 0.2 &&
                fabs(colchar->colobj->rigidbody->getLinearVelocity().getY()) <
                sy * 0.2) {
            colchar->frames_no_horizontal_velocity++;
        } else {
            colchar->frames_no_horizontal_velocity = 0;
        }
    }

    double distance_ground_thin = 0.0;
    double distance_ground_regular = 0.0;
    double distance_ground_corner_pxpy[3];
    double distance_ground_corner_pxny[3];
    double distance_ground_corner_nxpy[3];
    double distance_ground_corner_nxny[3];
    double ground_normal[3];
    if (!colworld_ObjectDoSweep(
            colchar->colobj, scanpos,
            &distance_ground_regular,
            NULL
            )) {
        distance_ground_regular = ourpos[2] - scanpos[2];
    }
    if (!colworld_ObjectDoSweepThin(
            colchar->colobj, scanpos,
            &distance_ground_thin,
            ground_normal
            )) {
        distance_ground_thin = ourpos[2] - scanpos[2];
        memset(ground_normal, 0, sizeof(*ground_normal));
        ground_normal[2] = 1.0;
    }
    double startpos[3];
    memcpy(startpos, ourpos, sizeof(startpos));
    startpos[0] += sx * 0.4;
    startpos[1] += sy * 0.4;
    double endpos[3];
    memcpy(endpos, scanpos, sizeof(endpos));
    endpos[0] += sx * 0.4;
    endpos[1] += sy * 0.4;
    if (!colworld_DoRaycast(
            colchar->colobj->cworld,
            startpos, endpos, colchar->colobj,
            distance_ground_corner_pxpy, NULL, NULL)) {
        distance_ground_corner_pxpy[0] = startpos[0];
        distance_ground_corner_pxpy[1] = startpos[1];
        distance_ground_corner_pxpy[2] = endpos[2];
    }
    memcpy(startpos, ourpos, sizeof(startpos));
    startpos[0] += sx * 0.4;
    startpos[1] -= sy * 0.4;
    memcpy(endpos, scanpos, sizeof(endpos));
    endpos[0] += sx * 0.4;
    endpos[1] -= sy * 0.4;
    if (!colworld_DoRaycast(
            colchar->colobj->cworld,
            startpos, endpos, colchar->colobj,
            distance_ground_corner_pxny, NULL, NULL)) {
        distance_ground_corner_pxny[0] = startpos[0];
        distance_ground_corner_pxny[1] = startpos[1];
        distance_ground_corner_pxny[2] = endpos[2];
    }
    memcpy(startpos, ourpos, sizeof(startpos));
    startpos[0] -= sx * 0.4;
    startpos[1] += sy * 0.4;
    memcpy(endpos, scanpos, sizeof(endpos));
    endpos[0] -= sx * 0.4;
    endpos[1] += sy * 0.4;
    if (!colworld_DoRaycast(
            colchar->colobj->cworld,
            startpos, endpos, colchar->colobj,
            distance_ground_corner_nxpy, NULL, NULL)) {
        distance_ground_corner_nxpy[0] = startpos[0];
        distance_ground_corner_nxpy[1] = startpos[1];
        distance_ground_corner_nxpy[2] = endpos[2];
    }
    memcpy(startpos, ourpos, sizeof(startpos));
    startpos[0] -= sx * 0.4;
    startpos[1] -= sy * 0.4;
    memcpy(endpos, scanpos, sizeof(endpos));
    endpos[0] -= sx * 0.4;
    endpos[1] -= sy * 0.4;
    if (!colworld_DoRaycast(
            colchar->colobj->cworld,
            startpos, endpos, colchar->colobj,
            distance_ground_corner_nxny, NULL, NULL)) {
        distance_ground_corner_nxny[0] = startpos[0];
        distance_ground_corner_nxny[1] = startpos[1];
        distance_ground_corner_nxny[2] = endpos[2];
    }
    double thinray_normal1[3];
    double thinray_normal2[3];
    polygon3d_normal(
        distance_ground_corner_pxny, distance_ground_corner_nxpy,
        distance_ground_corner_nxny,
        thinray_normal1
    );
    polygon3d_normal(
        distance_ground_corner_nxpy, distance_ground_corner_pxny,
        distance_ground_corner_pxpy,
        thinray_normal2
    );
    vec3_normalize(thinray_normal1, thinray_normal1);
    vec3_normalize(thinray_normal2, thinray_normal2);
    int gotpushdir = 0;
    double pushdir[3];
    pushdir[0] = (thinray_normal1[0] + thinray_normal2[0]) / 2.0;
    pushdir[1] = (thinray_normal1[1] + thinray_normal2[1]) / 2.0;
    pushdir[2] = 0;
    if (fabs(pushdir[0]) > 0.00001 ||
            fabs(pushdir[1]) > 0.00001) {
        vec3_normalize(pushdir, pushdir);
        gotpushdir = 1;
    }
    int superstrongslope = 0;
    int strongslope = 0;
    if ((distance_ground_corner_pxpy[2] < ourpos[2] -
                hover_intended - step_height * 1.7 ||
            distance_ground_corner_pxny[2] < ourpos[2] -
                hover_intended - step_height * 1.7 ||
            distance_ground_corner_nxpy[2] < ourpos[2] -
                hover_intended - step_height * 1.7 ||
            distance_ground_corner_nxny[2] < ourpos[2] -
                hover_intended - step_height * 1.7) &&
            (distance_ground_corner_pxpy[2] > ourpos[2] -
                hover_intended - step_height * 0.1 ||
            distance_ground_corner_pxny[2] > ourpos[2] -
                hover_intended - step_height * 0.1 ||
            distance_ground_corner_nxpy[2] > ourpos[2] -
                hover_intended - step_height * 0.1 ||
            distance_ground_corner_nxny[2] > ourpos[2] -
                hover_intended - step_height * 0.1) &&
            gotpushdir) {
        strongslope = 1;
        if ((distance_ground_corner_pxpy[2] < ourpos[2] -
                    hover_intended - step_height * 2.7 ||
                distance_ground_corner_pxny[2] < ourpos[2] -
                    hover_intended - step_height * 2.7 ||
                distance_ground_corner_nxpy[2] < ourpos[2] -
                    hover_intended - step_height * 2.7 ||
                distance_ground_corner_nxny[2] < ourpos[2] -
                    hover_intended - step_height * 2.7)) {
            superstrongslope = 1;
        }
    }
    colchar->onslope = (
        strongslope && superstrongslope &&
        (colchar->frames_no_horizontal_velocity < 10 ||
         colchar->colobj->rigidbody->getLinearVelocity().getZ() <
         -sz * 0.4)
    );

    // Calculate downforce_allowance to stop gravity when on ground:
    double downforce_allowance = 1.0;
    double gradual_range = step_height * 2;
    if (distance_ground_regular <= hover_intended + 0.001) {
        downforce_allowance = 0.0; 
    } else {
        double vertical_force = (
            colchar->colobj->rigidbody->getLinearVelocity().getZ()
        );
        double f = (
            (vertical_force + original_object_height * 0.3) / (
                original_object_height * (0.3 + 0.3 * timestep_f)
            )
        );
        if (f < 0)
            f = 0;
        if (f > 1)
            f = 1;
        if (distance_ground_regular > hover_intended + step_height * 0.5)
            f = 1;
        downforce_allowance = f * ((
            distance_ground_regular - hover_intended
        ) / gradual_range);
        if (downforce_allowance < 0.0)
            downforce_allowance = 0.0;
        if (downforce_allowance > 1.0)
            downforce_allowance = 1.0;
    }

    // Calculate upforce for getting pushed up stairs:
    double upforce = 0.0;
    if (distance_ground_thin < hover_intended - step_height * 0.1) {
        double f = 1.0;
        double vertical_force = (
            colchar->colobj->rigidbody->getLinearVelocity().getZ()
        );
        f = 1.0 - (
            (vertical_force + original_object_height * 0.3) / (
                original_object_height * 0.4
            )
        );
        if (f < 0)
            f = 0;
        if (f > 1)
            f = 1;
        if (vertical_force > 0)
            f = 0.2;
        upforce = f * (
            (hover_intended - distance_ground_thin) / step_height
        ) * 50 * (0.5 + 0.5 * timestep_f);
        if (strongslope)
            upforce = 0;
    }

    // Set high damping when on the ground, low when in the air:
    int apply_pushwall_even_if_on_ground = 0;
    int highdamp = 0;
    if (distance_ground_thin < hover_intended +
            step_height * 0.25 * timestep_f ||
            (distance_ground_regular < hover_intended &&
             colchar->frames_no_vertical_velocity > 20) ||
            (distance_ground_regular < hover_intended +
             step_height * 0.25 * timestep_f &&
             colchar->frames_no_vertical_velocity > 50)) {
        colchar->onfloor = 1;
        colchar->colobj->rigidbody->setDamping(0.98, 0.0);
        if (distance_ground_thin > hover_intended +
                step_height * 0.25 * timestep_f) {
            apply_pushwall_even_if_on_ground = 1;
        }
    } else {
        colchar->onfloor = 0;
        colchar->colobj->rigidbody->setDamping(0.001, 0.0);
    }

    // If on strong slope or possibly in the air, push away
    // from walls to prevent getting stuck:
    double wallpushdir[3];
    memset(wallpushdir, 0, sizeof(wallpushdir));
    int detectedpushwall = 0;
    if (strongslope || !colchar->onfloor ||
            apply_pushwall_even_if_on_ground) {
        charactercontroller_CalculateHalfheightSweeps(colchar);
        if (colchar->cached_lowheightsweeps_ypos >= 0 &&
                colchar->cached_lowheightsweeps_ypos <
                    sy * 0.1 + sy * 0.5) {
            wallpushdir[1] = -1.0;
            detectedpushwall = 1;
        } else if (colchar->cached_lowheightsweeps_yneg >= 0 &&
                colchar->cached_lowheightsweeps_yneg <
                    sy * 0.1 + sy * 0.5) {
            wallpushdir[1] = 1.0;
            detectedpushwall = 1;
        }
        if (colchar->cached_lowheightsweeps_xpos >= 0 &&
                colchar->cached_lowheightsweeps_xpos <
                    sx * 0.1 + sx * 0.5) {
            wallpushdir[0] = -1.0;
            detectedpushwall = 1;
        } else if (colchar->cached_lowheightsweeps_xneg >= 0 &&
                colchar->cached_lowheightsweeps_xneg <
                sx * 0.1 + sx * 0.5) {
            wallpushdir[0] = 1.0;
            detectedpushwall = 1;
        }
        if (detectedpushwall) {
            vec3_normalize(wallpushdir, wallpushdir);
            upforce = 0;
        }
    }

    #if defined(DEBUGCHARACTERPHYSICS) && !defined(NDEBUG)
    printf(
        "horse3d/collision/charactercontroller.cpp: verbose: "
        "CT%p: down_reg: %f  down_thin: %f  / hover_intended: %f\n",
        colchar, distance_ground_regular,
        distance_ground_thin, hover_intended
    );
    printf(
        "horse3d/collision/charactercontroller.cpp: verbose: "
        "CT%p: "
        "thinpxpy(%f, %f, %f), thinpxny(%f, %f, %f),"
        "thinnxpy(%f, %f, %f), thinnxny(%f, %f, %f)\n",
        colchar,
        distance_ground_corner_pxpy[0],
        distance_ground_corner_pxpy[1],
        distance_ground_corner_pxpy[2],
        distance_ground_corner_pxny[0],
        distance_ground_corner_pxny[1],
        distance_ground_corner_pxny[2],
        distance_ground_corner_nxpy[0],
        distance_ground_corner_nxpy[1],
        distance_ground_corner_nxpy[2],
        distance_ground_corner_nxny[0],
        distance_ground_corner_nxny[1],
        distance_ground_corner_nxny[2]
    );
    printf(
        "horse3d/collision/charactercontroller.cpp: verbose: "
        "CT%p: downfac:%f / upforce: %f, thin_normal1(%f, %f, %f), "
        "thin_normal2(%f, %f, %f)\n"
        "horse3d/collision/charactercontroller.cpp: verbose: CT%p: "
        "strongslope: %d (super: %d), highdamp: %d, pushvec(%f, %f, %f)\n"
        "horse3d/collision/charactercontroller.cpp: verbose: CT%p: "
        "NO-VFORCE frames: %d\n"
        "horse3d/collision/charactercontroller.cpp: verbose: CT%p: "
        "NO-HFORCE frames: %d\n"
        "horse3d/collision/charactercontroller.cpp: verbose: CT%p: "
        "wallpushdir(%f, %f, %f)\n",
        colchar, downforce_allowance, upforce,
        thinray_normal1[0], thinray_normal1[1], thinray_normal1[2],
        thinray_normal2[0], thinray_normal2[1], thinray_normal2[2],
        colchar, strongslope, superstrongslope,
        highdamp, pushdir[0], pushdir[1], pushdir[2],
        colchar, colchar->frames_no_vertical_velocity,
        colchar, colchar->frames_no_horizontal_velocity,
        colchar, wallpushdir[0], wallpushdir[1], wallpushdir[2]
    );
    #endif

    if (colchar->forcescount > 0) {
        #if defined(DEBUGCHARACTERPHYSICS) && !defined(NDEBUG)
        printf(
            "horse3d/collision/charactercontroller.cpp: verbose: "
            "CT%p: applying user forces:",
            colchar
        );
        #endif
        int i = 0;
        while (i < colchar->forcescount) {
            if (colchar->forces[3 * i + 2] < 0)
                colchar->forces[3 * i + 2] *= downforce_allowance;
            if (detectedpushwall) {
                if (wallpushdir[0] > 0.5 && colchar->forces[3 * i + 0] < 0)
                    colchar->forces[3 * i + 0] = 0;
                if (wallpushdir[0] < -0.5 && colchar->forces[3 * i + 0] > 0)
                    colchar->forces[3 * i + 0] = 0;
                if (wallpushdir[1] > 0.5 && colchar->forces[3 * i + 1] < 0)
                    colchar->forces[3 * i + 1] = 0;
                if (wallpushdir[1] < -0.5 && colchar->forces[3 * i + 1] > 0)
                    colchar->forces[3 * i + 1] = 0;
            }
            colworld_ObjectApplyForce(
                colchar->colobj, &colchar->forces[3 * i]
            );
            #if defined(DEBUGCHARACTERPHYSICS) && !defined(NDEBUG)
            printf(
                " %d/%d",
                i, colchar->forcescount
            );
            #endif
            i++;
        }
        #if defined(DEBUGCHARACTERPHYSICS) && !defined(NDEBUG)
        printf("\n");
        #endif
    } else {
        #if defined(DEBUGCHARACTERPHYSICS) && !defined(NDEBUG)
        printf(
            "horse3d/collision/charactercontroller.cpp: verbose: "
            "CT%p: applying no user force\n",
            colchar
        );
        #endif
    }
    colchar->forcescount = 0;

    // Apply actual gravity:
    double gravity[3];
    memset(gravity, 0, sizeof(gravity));
    if (!highdamp)
        forcedampbasedfactor = 1.0;
    gravity[0] = colchar->colobj->cworld->gravity_x * forcedampbasedfactor;
    gravity[1] = colchar->colobj->cworld->gravity_y * forcedampbasedfactor;
    if (highdamp || strongslope)
        gravity[2] = (
            colchar->colobj->cworld->gravity_z * forcedampbasedfactor *
            0.005 * mass
        );
    else
        gravity[2] = colchar->colobj->cworld->gravity_z *
            forcedampbasedfactor * mass;
    if (gravity[2] < 0)
        gravity[2] *= downforce_allowance;
    gravity[2] += upforce * forcescalefactor;
    if (strongslope) {  // Tweak gravity with slope slide force:
        double addf = (
            ((colchar->colobj->rigidbody->getLinearVelocity().getX() > 0 &&
              pushdir[0] > 0) ||
             (colchar->colobj->rigidbody->getLinearVelocity().getX() < 0 &&
              pushdir[0] < 0) || colchar->frames_no_vertical_velocity > 20) ?
            (superstrongslope ? 1.0 : 0.1) : (superstrongslope ? 10.0 : 1.0)
        );
        gravity[0] += pushdir[0] * 5 * addf *
            forcescalefactor * forcedampbasedfactor;
        addf = (
            ((colchar->colobj->rigidbody->getLinearVelocity().getY() > 0 &&
              pushdir[1] > 0) ||
             (colchar->colobj->rigidbody->getLinearVelocity().getY() < 0 &&
              pushdir[1] < 0) || colchar->frames_no_vertical_velocity > 20) ?
            (superstrongslope ? 1.0 : 0.1) : (superstrongslope ? 10.0 : 1.0)
        );
        gravity[1] += pushdir[1] * 5 * addf *
            forcescalefactor * forcedampbasedfactor;
        gravity[2] -= (superstrongslope ? 0.5 : 0) * forcescalefactor;
    }
    if (detectedpushwall) {  // Tweak gravity with sideways push wall force:
        gravity[0] += wallpushdir[0] * 4 *
            forcescalefactor * forcedampbasedfactor;
        gravity[1] += wallpushdir[1] * 4 *
            forcescalefactor * forcedampbasedfactor;
        gravity[2] += 0.5 * forcescalefactor;
    }
    colworld_ObjectApplyForce(
        colchar->colobj, gravity
    );
}

void charactercontroller_Destroy(h3dcolcharacter *colchar) {
    if (!colchar)
        return;

    free(colchar);
}

}  // extern "C"
