
#include <assert.h>
#define BT_USE_DOUBLE_PRECISION
#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionDispatch/btCollisionDispatcher.h>
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletDynamics/ConstraintSolver/btNNCGConstraintSolver.h>
extern "C" {
#include <mathc/mathc.h>
}
#include <SDL2/SDL.h>

extern "C" {
#include "colworld.h"
#include "colworld_internal.h"
#include "../datetime.h"
#include "../math3d.h"
}

#define MARGIN 0.01
#define MINOBJDIAMETER 0.2
#define TINYBODYTHRESHOLD 0.45

uint64_t physicsticks = 0;
int unfinalized_objects_count = 0;
h3dcolobject **unfinalized_objects = NULL;


class CustomSweepIgnoringObjectCallback :
        public btCollisionWorld::ClosestConvexResultCallback {
  public:
    btRigidBody *ignoredbody;

    CustomSweepIgnoringObjectCallback(
            btRigidBody *ignorebody,
            const btVector3 &fromV, const btVector3 &toV) :
            btCollisionWorld::ClosestConvexResultCallback(fromV, toV) {
        this->ignoredbody = ignorebody;
    }

    virtual bool needsCollision(btBroadphaseProxy *proxy) const {
        if (ignoredbody && ignoredbody == proxy->m_clientObject)
            return false;
        return (
            btCollisionWorld::ClosestConvexResultCallback::
                needsCollision(proxy)
        );
    }
};

class CustomRaycastIgnoringObjectCallback :
        public btCollisionWorld::ClosestRayResultCallback {
  public:
    btRigidBody *ignoredbody;

    CustomRaycastIgnoringObjectCallback(
            btRigidBody *ignorebody,
            const btVector3 &fromV, const btVector3 &toV) :
            btCollisionWorld::ClosestRayResultCallback(fromV, toV) {
        this->ignoredbody = ignorebody;
    }

    virtual bool needsCollision(btBroadphaseProxy *proxy) const {
        if (ignoredbody && ignoredbody == proxy->m_clientObject)
            return false;
        return (
            btCollisionWorld::ClosestRayResultCallback::
                needsCollision(proxy)
        );
    }
};

static void colworld_ApplyRigidBodySettings(
        btRigidBody *body, h3dcolobject *obj
        ) {
    assert(obj->_ccdminsize > 0 ||
           obj->type == COLWORLD_OTYPE_TRIANGLEMESH);
    int istiny = 0;
    if (obj->type != COLWORLD_OTYPE_TRIANGLEMESH) {
        istiny = (
            obj->_ccdminsize < TINYBODYTHRESHOLD * 0.5
        ) || (
            (obj->dimensions_x + obj->dimensions_y + obj->dimensions_z) / 3.0
            < TINYBODYTHRESHOLD
        );
    }
    if (istiny || obj->type == COLWORLD_OTYPE_TRIANGLEMESH) {
        double f = obj->friction;
        f = (1 - (1 - f) * 0.5);
        body->setFriction(f);
        body->setRestitution(0.0);
        body->setDamping(0.5, 0.7);
        body->setDeactivationTime(2.0);
        body->setSleepingThresholds(3, 8);
    } else {
        body->setFriction(obj->friction);
        body->setRestitution(obj->restitution);
        body->setDamping(0.3, 0.2);
        body->setDeactivationTime(3.0);
        body->setSleepingThresholds(2, 4);
    }
}


extern "C" {

void colworld_DisableObjectGravity(
        h3dcolobject *obj, int disabled
        ) {
    if (!obj)
        return;
    obj->disabledlocalgravity = (disabled != 0);
    if (obj->rigidbody && disabled) {
        obj->rigidbody->activate();
        obj->rigidbody->setGravity(btVector3(0, 0, 0));
    } else if (obj->rigidbody && !disabled) {
        obj->rigidbody->setGravity(
            btVector3(obj->cworld->gravity_x,
                      obj->cworld->gravity_y,
                      obj->cworld->gravity_z)
        );
    }
}

void colworld_SetFriction(
        h3dcolobject *obj, double friction
        ) {
    if (!obj)
        return;
    obj->friction = friction;
    if (obj->rigidbody)
        obj->rigidbody->setFriction(friction);
}

static void colworld_FetchObjTransform(h3dcolobject *obj) {
    if (!obj->rigidbody)
        return;

    btTransform bt;
    bt = obj->rigidbody->getWorldTransform();
    btVector3 pos = bt.getOrigin();
    obj->x = pos.getX();
    assert(!isNaN(obj->x));
    obj->y = pos.getY();
    assert(!isNaN(obj->y));
    obj->z = pos.getZ();
    assert(!isNaN(obj->z));
    btQuaternion rot = bt.getRotation();
    obj->rotx = rot.getX();
    assert(!isNaN(obj->rotx));
    obj->roty = rot.getY();
    assert(!isNaN(obj->roty));
    obj->rotz = rot.getZ();
    assert(!isNaN(obj->rotz));
    obj->rotw = rot.getW();
    assert(!isNaN(obj->rotw));
}

static void colworld_ResetPhysicsBody(h3dcolobject *obj) {
    if (!obj->rigidbody)
        return;

    btDiscreteDynamicsWorld *bulletworld = (
        (btDiscreteDynamicsWorld*)obj->cworld->bulletworldptr
    );
    bulletworld->getPairCache()->cleanProxyFromPairs(
        obj->rigidbody->getBroadphaseProxy(), bulletworld->getDispatcher()
    );
    bulletworld->updateSingleAabb(obj->rigidbody);
    obj->cworld->needAABBUpdate = 1;
    obj->rigidbody->activate();
}

static void colworld_StoreObjTransform(h3dcolobject *obj) {
    if (!obj->rigidbody)
        return;

    obj->rigidbody->activate();

    btVector3 pos;
    pos.setX(obj->x);
    pos.setY(obj->y);
    pos.setZ(obj->z);
    assert(!isNaN(pos.getX()));
    assert(!isNaN(pos.getY()));
    assert(!isNaN(pos.getZ()));
    btTransform transform = (
        obj->rigidbody->getWorldTransform()
    );
    transform.setOrigin(pos);
    obj->rigidbody->setWorldTransform(transform);
    obj->rigidbody->getMotionState()->setWorldTransform(transform);
    obj->rigidbody->setAngularVelocity(btVector3(0, 0, 0));
    obj->rigidbody->setLinearVelocity(btVector3(0, 0, 0));
    colworld_ResetPhysicsBody(obj);
}

double colworld_LimitToValidObjectScale(
        h3dcolobject *obj, double scale
        ) {
    double oldscale = scale;
    if (scale < 0.001)
        scale = 0.001;
    if (obj->type != COLWORLD_OTYPE_TRIANGLEMESH) {
        assert(obj->dimensions_x > 0 &&
               obj->dimensions_y > 0 &&
               obj->dimensions_z > 0);
        if (obj->dimensions_x > 0 &&
                obj->dimensions_x * scale < MINOBJDIAMETER)
            scale = MINOBJDIAMETER / obj->dimensions_x;
        if (obj->dimensions_y > 0 &&
                obj->dimensions_y * scale < MINOBJDIAMETER)
            scale = MINOBJDIAMETER / obj->dimensions_y;
        if (obj->dimensions_z > 0 &&
                obj->dimensions_z * scale < MINOBJDIAMETER)
            scale = MINOBJDIAMETER / obj->dimensions_z;
    }
    #ifndef NDEBUG
    if (scale != oldscale)
        fprintf(stderr,
            "horse3d/collision/colworld.cpp: warning: "
            "scale prevented from being too tiny: %f (old: %f)\n",
            scale, oldscale
        );
    #endif
    assert(scale > 0);
    return scale;
}

void colworld_SetObjectScale(h3dcolobject *obj, double scale) {
    scale = colworld_LimitToValidObjectScale(obj, scale);
    assert(scale > 0);
    obj->scale = scale;
    if (obj->rigidbody && obj->bodyshape) {
        assert(obj->scale > 0);
        obj->bodyshape->setLocalScaling(
            btVector3(obj->scale, obj->scale, obj->scale)
        );
        btVector3 inertia(0, 0, 0);
        if (obj->rigidbody->getMass() > 0.0001) {
            obj->bodyshape->calculateLocalInertia(
                obj->rigidbody->getMass(), inertia
            );
            obj->rigidbody->setMassProps(
                obj->rigidbody->getMass(), inertia
            );
        }
    }
    if (obj->rigidbody && obj->bodyshapethin) {
        assert(obj->scale > 0);
        obj->bodyshapethin->setLocalScaling(
            btVector3(obj->scale, obj->scale, obj->scale)
        );
    }
    if (obj->rigidbody && obj->bodyshapelowheight) {
        assert(obj->scale > 0);
        obj->bodyshapelowheight->setLocalScaling(
            btVector3(obj->scale, obj->scale, obj->scale)
        );
    }
    if (obj->rigidbody && obj->type != COLWORLD_OTYPE_TRIANGLEMESH) {
        obj->rigidbody->setCcdMotionThreshold(
            obj->_ccdminsize * 0.4 * obj->scale
        );
        obj->rigidbody->setCcdSweptSphereRadius(
            obj->_ccdminsize * 0.8 * obj->scale
        );
        colworld_ApplyRigidBodySettings(
            obj->rigidbody, obj
        );
    }
    colworld_ResetPhysicsBody(obj);
}

h3dcolworld *colworld_NewWorld() {
    h3dcolworld *cworld = (h3dcolworld*)malloc(sizeof(*cworld));
    if (!cworld)
        return NULL;
    memset(cworld, 0, sizeof(*cworld));
    cworld->gravity_z = -9;

    btDefaultCollisionConfiguration *config = (
        new btDefaultCollisionConfiguration()
    );
    btCollisionDispatcher *dispatcher = (
        new btCollisionDispatcher(config)
    );
    btBroadphaseInterface *broadphase = new btDbvtBroadphase();
    btConstraintSolver *solver = (
        new btNNCGConstraintSolver()
        //new btSequentialImpulseConstraintSolver()
    );
    cworld->bulletworldptr = new btDiscreteDynamicsWorld(
        dispatcher, broadphase, solver, config
	);
    ((btDiscreteDynamicsWorld*)cworld->bulletworldptr)
        ->getDispatchInfo().m_useContinuous = true;
    ((btDiscreteDynamicsWorld*)cworld->bulletworldptr)
        ->getSolverInfo().m_numIterations = 15;

    ((btDiscreteDynamicsWorld*)cworld->bulletworldptr)
        ->getSolverInfo().m_splitImpulse = true;
    btVector3 v;
    v.setX(cworld->gravity_x);
    v.setY(cworld->gravity_y);
    v.setZ(cworld->gravity_z);
    ((btDiscreteDynamicsWorld*)cworld->bulletworldptr)->setGravity(v);

    return cworld;
}

void colworld_DestroyWorld(h3dcolworld *cworld) {
    if (!cworld)
        return;
    if (cworld->bulletworldptr) {
        delete ((btCollisionWorld*)cworld->bulletworldptr);
    }
    free(cworld);
}

double colworld_GetEffectiveObjectScale(
        h3dcolobject *obj
        ) {
    return obj->scale;
}

void colworld_SetObjectFixedAngle(
        h3dcolobject *obj, int enabled
        ) {
    if (!obj)
        return;
    if (obj->rigidbody && enabled)
        obj->rigidbody->setAngularFactor(
            btVector3(0.0f, 0.0f, 0.0f)
        );
}

int _colworld_ObjectDoSweepEx(
        h3dcolobject *obj,
        int usethinshape, int uselowheightshape,
        const double _endpos[3],
        double *result_distanceToCollision,
        double result_normal[3]
        ) {
    if (!obj || !obj->rigidbody || !obj->shape_is_convex)
        return 0;

    if (usethinshape || uselowheightshape) {
        colworld_ConvertObjectToMultishape(obj);
        if (!obj->bodyshapethin || !obj->bodyshapelowheight)
            return 0;
    }

    try {
        double startpos[3];
        startpos[0] = obj->rigidbody->
            getWorldTransform().getOrigin().getX();
        startpos[1] = obj->rigidbody->
            getWorldTransform().getOrigin().getY();
        startpos[2] = obj->rigidbody->
            getWorldTransform().getOrigin().getZ();

        double endpos[3];
        endpos[0] = _endpos[0];
        endpos[1] = _endpos[1];
        endpos[2] = _endpos[2];

        btDiscreteDynamicsWorld *bulletworld = (
            (btDiscreteDynamicsWorld*)obj->cworld->bulletworldptr
        );
        if (obj->cworld->needAABBUpdate) {
            bulletworld->updateAabbs();
            obj->cworld->needAABBUpdate = 0;
        }
        btTransform raySourceTransform;
        raySourceTransform.setIdentity();
        raySourceTransform.setOrigin(btVector3(
            startpos[0], startpos[1], startpos[2]
        ));
        btTransform rayTargetTransform;
        rayTargetTransform.setIdentity();
        rayTargetTransform.setOrigin(btVector3(
            endpos[0], endpos[1], endpos[2]
        ));

        CustomSweepIgnoringObjectCallback callback(
            obj->rigidbody,
            btVector3(
                startpos[0], startpos[1], startpos[2]
            ),
            btVector3(
                endpos[0], endpos[1], endpos[2]
            )
        );
        btConvexShape *shape = (btConvexShape *)obj->bodyshape;
        if (usethinshape)
            shape = (btConvexShape *)obj->bodyshapethin;
        else if (uselowheightshape)
            shape = (btConvexShape *)obj->bodyshapelowheight;
        bulletworld->convexSweepTest(
            shape,
            raySourceTransform,
            rayTargetTransform,
            callback
        );
        if (callback.hasHit()) {
            double impact[3];
            impact[0] = callback.m_hitPointWorld.getX();
            impact[1] = callback.m_hitPointWorld.getY();
            impact[2] = callback.m_hitPointWorld.getZ();
            if (result_distanceToCollision) {
                *result_distanceToCollision = vec3_distance(
                    (double *)startpos, (double *)impact
                );
            }
            if (result_normal) {
                result_normal[0] = callback.m_hitNormalWorld.getX();
                result_normal[1] = callback.m_hitNormalWorld.getY();
                result_normal[2] = callback.m_hitNormalWorld.getZ();
            }
            return 1;
        }
    } catch (std::bad_alloc &ba) {
    }
    return 0;
}

int colworld_ObjectDoSweepThin(
        h3dcolobject *obj,
        const double endpos[3],
        double *result_distanceToCollision,
        double result_normal[3]
        ) {
    return _colworld_ObjectDoSweepEx(
        obj, 1, 0, endpos, result_distanceToCollision, result_normal
    );
}

int colworld_ObjectDoSweepLowHeight(
        h3dcolobject *obj,
        const double endpos[3],
        double *result_distanceToCollision,
        double result_normal[3]
        ) {
    return _colworld_ObjectDoSweepEx(
        obj, 0, 1, endpos, result_distanceToCollision, result_normal
    );
}

int colworld_ObjectDoSweep(
        h3dcolobject *obj,
        const double endpos[3],
        double *result_distanceToCollision,
        double result_normal[3]
        ) {
    return _colworld_ObjectDoSweepEx(
        obj, 0, 0, endpos, result_distanceToCollision, result_normal
    );
}

void colworld_ConvertObjectToMultishape(h3dcolobject *obj) {
    if (!obj || obj->bodyshapethin)
        return;
    const double thin_factor = 0.5;
    try {
        if (obj->type == COLWORLD_OTYPE_CAPSULE) {
            double size_radius = obj->dimensions_x * thin_factor * 0.5;
            if (size_radius > obj->dimensions_y * thin_factor * 0.5)
                size_radius = obj->dimensions_y * thin_factor * 0.5;
            double size_height = obj->dimensions_z;
            obj->bodyshapethin = new btCapsuleShapeZ(
                size_radius - MARGIN * 0.1,
                size_height - (
                    (size_radius - MARGIN * 0.1) * 2
                ) - MARGIN * 0.1
            );
            assert(obj->scale > 0);
            obj->bodyshapethin->setLocalScaling(
                btVector3(obj->scale, obj->scale, obj->scale)
            );
            size_radius = obj->dimensions_x * 0.5;
            if (size_radius > obj->dimensions_y * 0.5)
                size_radius = obj->dimensions_y * 0.5;
            size_height = obj->dimensions_z * 0.33;
            obj->bodyshapelowheight = new btCapsuleShapeZ(
                size_radius - MARGIN * 0.1,
                size_height - (
                    (size_radius - MARGIN * 0.1) * 2
                ) - MARGIN * 0.1
            );
            assert(obj->scale > 0);
            obj->bodyshapelowheight->setLocalScaling(
                btVector3(obj->scale, obj->scale, obj->scale)
            );
        } else if (obj->type == COLWORLD_OTYPE_BOX) {
            btVector3 extends;
            extends.setX(obj->dimensions_x * 0.5 * thin_factor);
            extends.setY(obj->dimensions_y * 0.5 * thin_factor);
            extends.setZ(obj->dimensions_z);
            obj->bodyshapethin = new btBoxShape(extends);
            assert(obj->scale > 0);
            obj->bodyshapethin->setLocalScaling(
                btVector3(obj->scale, obj->scale, obj->scale)
            );
            extends.setX(obj->dimensions_x);
            extends.setY(obj->dimensions_y);
            extends.setZ(obj->dimensions_z * 0.33);
            obj->bodyshapelowheight = new btBoxShape(extends);
            assert(obj->scale > 0);
            obj->bodyshapelowheight->setLocalScaling(
                btVector3(obj->scale, obj->scale, obj->scale)
            );
        }
    } catch (std::bad_alloc &ba) {
        if (obj->bodyshapethin)
            delete obj->bodyshapethin;
        if (obj->bodyshapelowheight)
            delete obj->bodyshapelowheight;
        obj->bodyshapethin = NULL;
        obj->bodyshapelowheight = NULL;
    }
}

void colworld_ObjectApplyForce(
        h3dcolobject *obj, double force[3]
        ) {
    if (!obj || !obj->rigidbody)
        return;
    obj->rigidbody->activate();
    obj->rigidbody->applyCentralForce(
        btVector3(force[0], force[1], force[2])
    );
}

double colworld_GetObjectMass(h3dcolobject *obj) {
    if (!obj || !obj->rigidbody)
        return 0;
    return obj->rigidbody->getMass();
}

void colworld_SetObjectMass(h3dcolobject *obj, double mass) {
    if (!obj || !obj->rigidbody)
        return;
    btVector3 inertia(0, 0, 0);
    if (mass > 0.0001)
        obj->bodyshape->calculateLocalInertia(mass, inertia);
    obj->rigidbody->setMassProps(mass, inertia);
}

int colworld_DoRaycast(
        h3dcolworld *cworld,
        const double from[3], const double to[3],
        h3dcolobject *optional_ignore_object,
        double result_impact[3], double result_normal[3],
        double *result_distance
        ) {
    btDiscreteDynamicsWorld *bulletworld = (
        (btDiscreteDynamicsWorld*)cworld->bulletworldptr
    );
    btVector3 fromv(from[0], from[1], from[2]);
    btVector3 tov(to[0], to[1], to[2]);
    CustomRaycastIgnoringObjectCallback resultcb(
        optional_ignore_object->rigidbody, fromv, tov
    );
    bulletworld->rayTest(fromv, tov, resultcb);
    if (resultcb.hasHit()) {
        double resultpos[3];
        resultpos[0] = resultcb.m_hitPointWorld.getX();
        resultpos[1] = resultcb.m_hitPointWorld.getY();
        resultpos[2] = resultcb.m_hitPointWorld.getZ();
        if (result_impact)
            memcpy(result_impact, resultpos, sizeof(resultpos));
        if (result_normal) {
            result_normal[0] = resultcb.m_hitNormalWorld.getX();
            result_normal[1] = resultcb.m_hitNormalWorld.getY();
            result_normal[2] = resultcb.m_hitNormalWorld.getZ();
        }
        if (result_distance)
            *result_distance = vec3_distance(
                (double *)from, resultpos
            );
        return 1;
    }
    return 0;
}

h3dcolobject *colworld_NewCapsuleObject(
        h3dcolworld *cworld,
        double size_w, double size_h,
        double mass, double friction) {
    btDiscreteDynamicsWorld *bulletworld = (
        (btDiscreteDynamicsWorld*)cworld->bulletworldptr
    );
    if (size_w < MINOBJDIAMETER * 0.3)
        size_w = MINOBJDIAMETER * 0.3;
    if (size_h < MINOBJDIAMETER * 0.3)
        size_h = MINOBJDIAMETER * 0.3;

    h3dcolobject *obj = (h3dcolobject*)malloc(sizeof(*obj));
    if (!obj)
        return NULL;
    memset(obj, 0, sizeof(*obj));
    obj->type = COLWORLD_OTYPE_CAPSULE;
    obj->cworld = cworld;
    obj->friction = 0.6;
    obj->restitution = 0.5;
    obj->dimensions_x = size_w;
    obj->dimensions_y = size_w;
    obj->dimensions_z = size_h;
    obj->scale = 1;
    obj->scale = colworld_LimitToValidObjectScale(obj, obj->scale);

    double size_radius = size_w * 0.5;
    double size_capsheight = size_h - size_radius * 2;
    if (size_radius < 0.0001 + MARGIN * 0.1 ||
            size_capsheight < 0.0001 + MARGIN * 0.1) {
        free(obj);
        return NULL;
    }

    btDefaultMotionState *mstate = NULL;
    btCollisionShape* shape = NULL;
    try {
        shape = new btCapsuleShapeZ(
            size_radius - MARGIN * 0.1, size_capsheight - MARGIN * 0.1
        );
        shape->setMargin(MARGIN);
        shape->setLocalScaling(
            btVector3(obj->scale, obj->scale, obj->scale)
        );

        btVector3 inertia(0, 0, 0);
        if (mass > 0.0001)
            shape->calculateLocalInertia(mass, inertia);
        mstate =
            new btDefaultMotionState(
                btTransform(btQuaternion(0, 0, 0, 1),
                            btVector3(obj->x, obj->y, obj->z))
            );
        btRigidBody::btRigidBodyConstructionInfo cinfo(
            mass, mstate, shape, inertia
        );
        cinfo.m_additionalDamping = true;
        obj->rigidbody = new btRigidBody(cinfo);

        obj->shape_is_convex = 1;
        obj->mstate = mstate;
        obj->bodyshape = shape;
        double minsize = size_w;
        if (size_h < minsize)
            minsize = size_h;
        obj->_ccdminsize = minsize;
        colworld_ApplyRigidBodySettings(
            obj->rigidbody, obj
        );
        obj->rigidbody->setCcdMotionThreshold(
            obj->_ccdminsize * 0.4 * obj->scale
        );
        obj->rigidbody->setCcdSweptSphereRadius(
            obj->_ccdminsize * 0.8 * obj->scale
        );
        bulletworld->addRigidBody(obj->rigidbody);
    } catch (std::bad_alloc &ba) {
        if (obj->rigidbody)
            delete obj->rigidbody;
        if (mstate)
            delete mstate;
        if (shape)
            delete shape;
        free(obj);
        return NULL;
    }
    cworld->needAABBUpdate = 1;
    return obj;
}

h3dcolobject *colworld_NewBoxObject(
        h3dcolworld *cworld,
        double size_x, double size_y, double size_z,
        double mass, double friction
        ) {
    btDiscreteDynamicsWorld *bulletworld = (
        (btDiscreteDynamicsWorld*)cworld->bulletworldptr
    );
    if (size_x < MINOBJDIAMETER * 0.3)
        size_x = MINOBJDIAMETER * 0.3;
    if (size_y < MINOBJDIAMETER * 0.3)
        size_y = MINOBJDIAMETER * 0.3;
    if (size_z < MINOBJDIAMETER * 0.3)
        size_z = MINOBJDIAMETER * 0.3;

    h3dcolobject *obj = (h3dcolobject*)malloc(sizeof(*obj));
    if (!obj)
        return NULL;
    memset(obj, 0, sizeof(*obj));
    obj->type = COLWORLD_OTYPE_BOX;
    obj->cworld = cworld;
    obj->friction = 0.6;
    obj->restitution = 0.5;
    obj->dimensions_x = size_x;
    obj->dimensions_y = size_y;
    obj->dimensions_z = size_z;
    obj->scale = 1;
    obj->scale = colworld_LimitToValidObjectScale(obj, obj->scale);

    btVector3 extends;
    extends.setX(size_x * 0.5);
    extends.setY(size_y * 0.5);
    extends.setZ(size_z * 0.5);

    btCollisionShape* shape = NULL;
    btDefaultMotionState *mstate = NULL;
    try {
        shape = new btBoxShape(extends);
        if (!shape) {
            free(obj);
            return NULL;
        }
        shape->setMargin(MARGIN);
        shape->setLocalScaling(
            btVector3(obj->scale, obj->scale, obj->scale)
        );

        btVector3 inertia(0, 0, 0);
        if (mass > 0.0001)
            shape->calculateLocalInertia(mass, inertia);
        mstate =
            new btDefaultMotionState(
                btTransform(btQuaternion(0, 0, 0, 1),
                            btVector3(obj->x, obj->y, obj->z))
            );
        if (!mstate) {
            delete(shape);
            free(obj);
            return NULL;
        }
        btRigidBody::btRigidBodyConstructionInfo cinfo(
            mass, mstate, shape, inertia
        );
        cinfo.m_additionalDamping = true;
        obj->rigidbody = new btRigidBody(cinfo);
        if (!obj->rigidbody) {
            delete(mstate);
            delete(shape);
            free(obj);
            return NULL;
        }
        obj->shape_is_convex = 1;
        obj->mstate = mstate;
        obj->bodyshape = shape;
        double minsize = size_x;
        if (size_y < minsize)
            minsize = size_y;
        if (size_z < minsize)
            minsize = size_z;
        obj->_ccdminsize = minsize;
        colworld_ApplyRigidBodySettings(
            obj->rigidbody, obj
        );
        obj->rigidbody->setCcdMotionThreshold(
            obj->_ccdminsize * 0.4 * obj->scale
        );
        obj->rigidbody->setCcdSweptSphereRadius(
            obj->_ccdminsize * 0.8 * obj->scale
        );
        bulletworld->addRigidBody(obj->rigidbody);
        cworld->needAABBUpdate = 1;
    } catch (std::bad_alloc &ba) {
        if (obj->rigidbody)
            delete obj->rigidbody;
        if (mstate)
            delete mstate;
        if (shape)
            delete shape;
        free(obj);
        return NULL;
    }
    return obj;
}

static int colworld_QueueTriangleMeshObjAsUnfinalized(
        h3dcolobject *obj
        ) {
    int i = 0;
    while (i < unfinalized_objects_count) {
        if (unfinalized_objects[i] == obj)
            return 1;
        i++;
    }
    h3dcolobject **new_unfinalized_objects = (
        (h3dcolobject **)realloc(
            unfinalized_objects,
            sizeof(*new_unfinalized_objects) *
            (unfinalized_objects_count + 1)
        ));
    if (!new_unfinalized_objects)
        return 0;
    unfinalized_objects = new_unfinalized_objects;
    unfinalized_objects[unfinalized_objects_count] = obj;
    unfinalized_objects_count++;
    return 1;
}

h3dcolobject *colworld_NewTriangleMeshObject(
        h3dcolworld *cworld
        ) {
    btDiscreteDynamicsWorld *bulletworld = (
        (btDiscreteDynamicsWorld*)cworld->bulletworldptr
    );

    h3dcolobject *obj = (h3dcolobject*)malloc(sizeof(*obj));
    if (!obj)
        return NULL;
    memset(obj, 0, sizeof(*obj));
    obj->type = COLWORLD_OTYPE_TRIANGLEMESH;
    obj->cworld = cworld;
    obj->shape_is_convex = 0;
    obj->restitution = 0.5;
    obj->friction = 0.6;
    obj->scale = 1;
    return obj;
}

void colworld_DestroyBulletBodies(h3dcolobject *obj) {
    colworld_FetchObjTransform(obj);
    ((btDiscreteDynamicsWorld*)obj->cworld->bulletworldptr)->
        removeCollisionObject(obj->rigidbody);
    delete obj->rigidbody;
    if (obj->mstate)
        delete obj->mstate;
    if (obj->bodyshape)
        delete obj->bodyshape;
    if (obj->bodyshapethin)
        delete obj->bodyshapethin;
    obj->rigidbody = NULL;
    obj->bodyshape = NULL;
    obj->bodyshapethin = NULL;
    obj->mstate = NULL;
}

int colworld_AddTriangleMeshPolygon(
        h3dcolobject *obj,
        double pos1x, double pos1y, double pos1z, 
        double pos2x, double pos2y, double pos2z,
        double pos3x, double pos3y, double pos3z
        ) {
    assert(pos1x != pos2x || pos1y != pos2y || pos1z != pos2z);
    assert(pos1x != pos3x || pos1y != pos3y || pos1z != pos3z);
    assert(pos3x != pos2x || pos3y != pos2y || pos3z != pos2z);
    assert(
        !isNaN(pos1x) && !isNaN(pos1y) && !isNaN(pos1z) &&
        !isNaN(pos2x) && !isNaN(pos2y) && !isNaN(pos2z) &&
        !isNaN(pos3x) && !isNaN(pos3y) && !isNaN(pos3z)
    );

    assert(obj->type == COLWORLD_OTYPE_TRIANGLEMESH);
    if (obj->rigidbody || obj->trianglemeshfinalized || !obj->trianglemesh) {
        // We need to recreate a new mesh.
        obj->trianglemeshfinalized = 0;
        if (obj->rigidbody) {
            if (!colworld_QueueTriangleMeshObjAsUnfinalized(
                    obj))
                return 0;
            colworld_FetchObjTransform(obj);
            colworld_DestroyBulletBodies(obj);    
        }
        if (obj->trianglemesh)
            delete obj->trianglemesh;
        obj->trianglemesh = new btTriangleMesh();
        if (!obj->trianglemesh)
            return 0;
        if (!colworld_QueueTriangleMeshObjAsUnfinalized(obj))
            return 0;
    }
    btVector3 v1, v2, v3;
    v1 = btVector3(pos1x, pos1y, pos1z);
    v2 = btVector3(pos2x, pos2y, pos2z);
    v3 = btVector3(pos3x, pos3y, pos3z);
    obj->trianglemesh->addTriangle(v1, v2, v3);
    return 1;
}

static int colworld_FinalizeObject(
        h3dcolobject *obj
        ) {
    if (obj->type != COLWORLD_OTYPE_TRIANGLEMESH ||
            obj->trianglemeshfinalized)
        return 1;
    if (obj->rigidbody) {
        if (!colworld_QueueTriangleMeshObjAsUnfinalized(
                obj))
            return 0;
        colworld_FetchObjTransform(obj);
        colworld_DestroyBulletBodies(obj);
    }
    btCollisionShape *shape = new btBvhTriangleMeshShape(
        obj->trianglemesh, true, true
    );
    if (!shape) {
        return 0;
    }
    assert(obj->scale > 0);
    shape->setLocalScaling(
        btVector3(obj->scale, obj->scale, obj->scale)
    );

    btVector3 inertia(0, 0, 0);
    btDefaultMotionState *mstate =
        new btDefaultMotionState(
            btTransform(btQuaternion(0, 0, 0, 1),
                        btVector3(obj->x, obj->y, obj->z)));
    if (!mstate) {
        delete(shape);
        return 0;
    }
    btRigidBody::btRigidBodyConstructionInfo cinfo(
        0, mstate, shape, inertia
    );
    cinfo.m_additionalDamping = true;
    obj->rigidbody = new btRigidBody(cinfo);
    if (!obj->rigidbody) {
        delete(mstate);
        delete(shape);
        return 0;
    }
    colworld_ApplyRigidBodySettings(obj->rigidbody, obj);
    obj->shape_is_convex = 0;
    delete(mstate);
    obj->trianglemeshfinalized = 1;
    obj->bodyshape = shape;
    ((btDiscreteDynamicsWorld*)obj->cworld->bulletworldptr)->
        addRigidBody(obj->rigidbody);
    obj->cworld->needAABBUpdate = 1;
    return 1;
}

int colworld_UpdateWorld(
        h3dcolworld *cworld, double *dt,
        void (*stepCallback)(h3dcolworld *world, double dt)
        ) {
    btDiscreteDynamicsWorld *bulletworld = (
        (btDiscreteDynamicsWorld*)cworld->bulletworldptr
    );

    double stepped_dt = 0;
    if (unfinalized_objects_count > 0) {
        int hadfailure = 0;
        int i = 0;
        while (i < unfinalized_objects_count) {
            if (!unfinalized_objects[i]) {
                i++;
                continue;
            }
            if (colworld_FinalizeObject(unfinalized_objects[i]))
                unfinalized_objects[i] = NULL;
            else
                hadfailure = 1;
            i++;
        }
        if (!hadfailure) {
            free(unfinalized_objects);
            unfinalized_objects = NULL;
            unfinalized_objects_count = 0;
        }
    }

    int maxsteps = 10;
    int didstep = 0;
    uint64_t now = datetime_Ticks();
    while (physicsticks < now && didstep < maxsteps) {
        didstep++;
        const btScalar seconds = ((double)PHYSICS_STEP_MS) * 0.001;
        bulletworld->stepSimulation(seconds, 0, seconds);
        cworld->needAABBUpdate = 0;
        physicsticks += PHYSICS_STEP_MS;
        stepped_dt += PHYSICS_STEP_MS;
        if (stepCallback)
            stepCallback(cworld, PHYSICS_STEP_MS);
    }
    if (dt)
        *dt = stepped_dt;
    return didstep;
}

void colworld_GetObjectOrientation(h3dcolobject *obj, double *quat) {
    if (!obj || !obj->rigidbody) {
        memset(quat, 0, sizeof(*quat) * 4);
        return;
    }
    btQuaternion orientation = obj->rigidbody->getOrientation();
    quat[0] = orientation.x();
    quat[1] = orientation.y();
    quat[2] = orientation.z();
    quat[3] = orientation.w();
}

void colworld_DestroyObject(h3dcolobject *obj) {
    if (!obj)
        return;
    int i = 0;
    while (i < unfinalized_objects_count) {
        if (unfinalized_objects[i] == obj)
            unfinalized_objects[i] = NULL;
        i++;
    }
    colworld_DestroyBulletBodies(obj);
    if (obj->trianglemesh)
        delete obj->trianglemesh;
    free(obj);
}

void colworld_GetObjectPosition(
        h3dcolobject *obj, double *x, double *y, double *z
        ) {
    colworld_FetchObjTransform(obj);
    *x = obj->x;
    *y = obj->y;
    *z = obj->z;
}

void colworld_SetObjectPosition(
        h3dcolobject *obj, double x, double y, double z
        ) {
    colworld_FetchObjTransform(obj);
    obj->x = x;
    obj->y = y;
    obj->z = z;
    colworld_StoreObjTransform(obj);
    obj->cworld->needAABBUpdate = 1;
}

void colworld_GetObjectRotation(
        h3dcolobject *obj, double *quaternion
        ) {
    colworld_FetchObjTransform(obj);
    quaternion[0] = obj->rotx;
    quaternion[1] = obj->roty;
    quaternion[2] = obj->rotz;
    quaternion[3] = obj->rotw;
}

void colworld_SetObjectRotation(
        h3dcolobject *obj, double *quaternion
        ) {
    colworld_FetchObjTransform(obj);
    obj->rotx = quaternion[0];
    obj->roty = quaternion[1];
    obj->rotz = quaternion[2];
    obj->rotw = quaternion[3];
    colworld_StoreObjTransform(obj);
    obj->cworld->needAABBUpdate = 1;
}

}  // extern "C"
