/// @file physics_world.cpp
/// @brief Concrete implementation of PhysicsWorld using Jolt Physics.

#include "physics/physics_world.h"

#include "jolt_bridge.h"
#include "physics/physics_system.h"

#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <memory>
#include <mutex>
#include <vector>

namespace eng::physics {

class ContactListenerImpl final : public JPH::ContactListener {
public:
    void setPhysicsWorldImpl(PhysicsWorld::Impl* impl) noexcept { impl_ = impl; }

    JPH::ValidateResult
    OnContactValidate(const JPH::Body& /*inBody1*/, const JPH::Body& /*inBody2*/,
                      JPH::RVec3Arg /*inBaseOffset*/,
                      const JPH::CollideShapeResult& /*inCollisionResult*/) override {
        return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2,
                        const JPH::ContactManifold& inManifold,
                        JPH::ContactSettings& /*ioSettings*/) override;

    void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2,
                            const JPH::ContactManifold& inManifold,
                            JPH::ContactSettings& /*ioSettings*/) override;

    void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override;

    std::vector<CollisionEvent> takeEvents() {
        std::scoped_lock lock(eventsMutex_);
        std::vector<CollisionEvent> result;
        result.swap(events_);
        return result;
    }

    void clearEvents() noexcept {
        std::scoped_lock lock(eventsMutex_);
        events_.clear();
        activeEvents_.clear();
        eventsSpan_ = {};
    }

    [[nodiscard]] std::span<const CollisionEvent> getEvents() const noexcept { return eventsSpan_; }

    void updateSpan() noexcept {
        std::scoped_lock lock(eventsMutex_);
        activeEvents_ = events_;
        eventsSpan_ = activeEvents_;
    }

private:
    PhysicsWorld::Impl* impl_{nullptr};
    mutable std::mutex eventsMutex_;
    std::vector<CollisionEvent> events_;
    std::vector<CollisionEvent> activeEvents_;
    std::span<const CollisionEvent> eventsSpan_;
};

struct PhysicsWorld::Impl {
    struct BodySlot {
        JPH::BodyID bodyId{JPH::BodyID::cInvalidBodyID};
        uint32_t generation{1};
        bool alive{false};
        void* userData{nullptr};
    };

    explicit Impl(const PhysicsWorldSettings& settings) : settings_(settings) {
        // Ensure global initialization
        (void)PhysicsSystem::initialize();

        if (settings_.numWorkerThreads == 0) {
            jobSystem_ = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
        } else {
            jobSystem_ = std::make_unique<JPH::JobSystemThreadPool>(
                JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                static_cast<int>(settings_.numWorkerThreads));
        }

        contactListener_.setPhysicsWorldImpl(this);

        physicsSystem_.Init(settings_.maxBodies, settings_.numBodyMutexes, settings_.maxBodyPairs,
                            settings_.maxContactConstraints, broadPhaseLayerInterface_,
                            objectVsBroadphaseLayerFilter_, objectVsObjectLayerFilter_);

        physicsSystem_.SetContactListener(&contactListener_);
        physicsSystem_.SetGravity(toJolt(settings_.gravity));
    }

    ~Impl() noexcept {
        // Remove and destroy all alive bodies
        auto& bodyInterface = physicsSystem_.GetBodyInterface();
        for (auto& slot : slots_) {
            if (slot.alive && !slot.bodyId.IsInvalid()) {
                bodyInterface.RemoveBody(slot.bodyId);
                bodyInterface.DestroyBody(slot.bodyId);
                slot.alive = false;
            }
        }
    }

    [[nodiscard]] BodyHandle resolveHandle(uint32_t slotIdx) const noexcept {
        if (slotIdx < slots_.size()) {
            return BodyHandle{slotIdx, slots_[slotIdx].generation};
        }
        return BodyHandle{};
    }

    std::unique_ptr<JPH::JobSystem> jobSystem_;
    JPH::PhysicsSystem physicsSystem_;
    BPLayerInterfaceImpl broadPhaseLayerInterface_;
    ObjectVsBroadPhaseLayerFilterImpl objectVsBroadphaseLayerFilter_;
    ObjectLayerPairFilterImpl objectVsObjectLayerFilter_;
    std::vector<BodySlot> slots_;
    std::vector<uint32_t> freeSlots_;
    JPH::TempAllocatorMalloc tempAllocator_;
    ContactListenerImpl contactListener_;
    float accumulatedTime_{0.0F};
    uint32_t aliveCount_{0};
    PhysicsWorldSettings settings_;
};

void ContactListenerImpl::OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2,
                                         const JPH::ContactManifold& inManifold,
                                         JPH::ContactSettings& /*ioSettings*/) {
    if (impl_ == nullptr) {
        return;
    }
    const auto slotIdx1 = static_cast<uint32_t>(inBody1.GetUserData());
    const auto slotIdx2 = static_cast<uint32_t>(inBody2.GetUserData());

    CollisionEvent evt{
        .type = CollisionEventType::ContactAdded,
        .bodyA = impl_->resolveHandle(slotIdx1),
        .bodyB = impl_->resolveHandle(slotIdx2),
        .contactPoint = fromJolt(inManifold.GetWorldSpaceContactPointOn1(0)),
        .contactNormal = fromJolt(inManifold.mWorldSpaceNormal),
    };

    std::scoped_lock lock(eventsMutex_);
    events_.push_back(evt);
}

void ContactListenerImpl::OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2,
                                             const JPH::ContactManifold& inManifold,
                                             JPH::ContactSettings& /*ioSettings*/) {
    if (impl_ == nullptr) {
        return;
    }
    const auto slotIdx1 = static_cast<uint32_t>(inBody1.GetUserData());
    const auto slotIdx2 = static_cast<uint32_t>(inBody2.GetUserData());

    CollisionEvent evt{
        .type = CollisionEventType::ContactPersisted,
        .bodyA = impl_->resolveHandle(slotIdx1),
        .bodyB = impl_->resolveHandle(slotIdx2),
        .contactPoint = fromJolt(inManifold.GetWorldSpaceContactPointOn1(0)),
        .contactNormal = fromJolt(inManifold.mWorldSpaceNormal),
    };

    std::scoped_lock lock(eventsMutex_);
    events_.push_back(evt);
}

void ContactListenerImpl::OnContactRemoved(const JPH::SubShapeIDPair& /*inSubShapePair*/) {
    // Contact removed event
}

PhysicsWorld::PhysicsWorld(const PhysicsWorldSettings& settings)
    : impl_(std::make_unique<Impl>(settings)) {}

PhysicsWorld::~PhysicsWorld() noexcept = default;

PhysicsWorld::PhysicsWorld(PhysicsWorld&&) noexcept = default;
PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&&) noexcept = default;

Result<BodyHandle> PhysicsWorld::createBody(const BodyDesc& desc) {
    if (!impl_) {
        return Error{.code = ErrorCode::InvalidState, .message = "PhysicsWorld is uninitialized"};
    }

    auto shape = createJoltShape(desc.collider);
    if (shape == nullptr) {
        return Error{.code = ErrorCode::ResourceCreationFailed,
                     .message = "Failed to create Jolt collision shape"};
    }

    // Allocate slot
    uint32_t slotIdx = 0;
    if (!impl_->freeSlots_.empty()) {
        slotIdx = impl_->freeSlots_.back();
        impl_->freeSlots_.pop_back();
    } else {
        slotIdx = static_cast<uint32_t>(impl_->slots_.size());
        impl_->slots_.push_back(Impl::BodySlot{});
    }

    auto& slot = impl_->slots_[slotIdx];
    const auto activation = (desc.motionType == MotionType::Static) ? JPH::EActivation::DontActivate
                                                                    : JPH::EActivation::Activate;

    JPH::BodyCreationSettings bodySettings(shape, toJolt(desc.position), toJolt(desc.rotation),
                                           toJolt(desc.motionType), desc.collisionLayer);

    bodySettings.mLinearVelocity = toJolt(desc.linearVelocity);
    bodySettings.mAngularVelocity = toJolt(desc.angularVelocity);
    bodySettings.mFriction = desc.friction;
    bodySettings.mRestitution = desc.restitution;
    bodySettings.mGravityFactor = desc.gravityFactor;
    bodySettings.mLinearDamping = desc.linearDamping;
    bodySettings.mAngularDamping = desc.angularDamping;
    bodySettings.mIsSensor = desc.isSensor;
    bodySettings.mUserData = static_cast<uint64_t>(slotIdx);

    if (desc.motionType == MotionType::Dynamic && desc.mass > 0.0F) {
        bodySettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        bodySettings.mMassPropertiesOverride.mMass = desc.mass;
    }

    auto& bodyInterface = impl_->physicsSystem_.GetBodyInterface();
    JPH::Body* body = bodyInterface.CreateBody(bodySettings);
    if (body == nullptr) {
        impl_->freeSlots_.push_back(slotIdx);
        return Error{.code = ErrorCode::ResourceCreationFailed,
                     .message = "Jolt CreateBody returned null"};
    }

    bodyInterface.AddBody(body->GetID(), activation);

    slot.bodyId = body->GetID();
    slot.alive = true;
    slot.userData = desc.userData;
    ++impl_->aliveCount_;

    return BodyHandle{slotIdx, slot.generation};
}

Result<void> PhysicsWorld::destroyBody(BodyHandle handle) {
    if (!isBodyValid(handle)) {
        return Error{.code = ErrorCode::HandleResolutionFailed,
                     .message = "Attempted to destroy invalid or stale body handle"};
    }

    auto& slot = impl_->slots_[handle.index];
    auto& bodyInterface = impl_->physicsSystem_.GetBodyInterface();

    bodyInterface.RemoveBody(slot.bodyId);
    bodyInterface.DestroyBody(slot.bodyId);

    slot.bodyId = JPH::BodyID();
    slot.alive = false;
    slot.userData = nullptr;
    ++slot.generation;
    if (impl_->aliveCount_ > 0) {
        --impl_->aliveCount_;
    }

    impl_->freeSlots_.push_back(handle.index);
    return Result<void>::success();
}

bool PhysicsWorld::isBodyValid(BodyHandle handle) const noexcept {
    if (!impl_ || !handle.isValid() || handle.index >= impl_->slots_.size()) {
        return false;
    }
    const auto& slot = impl_->slots_[handle.index];
    return slot.alive && slot.generation == handle.generation;
}

uint32_t PhysicsWorld::getBodyCount() const noexcept {
    return impl_ ? impl_->aliveCount_ : 0;
}

Result<Vec3> PhysicsWorld::getPosition(BodyHandle handle) const {
    if (!isBodyValid(handle)) {
        return Error{.code = ErrorCode::HandleResolutionFailed, .message = "Invalid BodyHandle"};
    }
    const auto& bodyInterface = impl_->physicsSystem_.GetBodyInterface();
    const auto pos = bodyInterface.GetPosition(impl_->slots_[handle.index].bodyId);
    return fromJolt(pos);
}

Result<void> PhysicsWorld::setPosition(BodyHandle handle, const Vec3& position) {
    if (!isBodyValid(handle)) {
        return Error{.code = ErrorCode::HandleResolutionFailed, .message = "Invalid BodyHandle"};
    }
    auto& bodyInterface = impl_->physicsSystem_.GetBodyInterface();
    bodyInterface.SetPosition(impl_->slots_[handle.index].bodyId, toJolt(position),
                              JPH::EActivation::Activate);
    return Result<void>::success();
}

Result<Quat> PhysicsWorld::getRotation(BodyHandle handle) const {
    if (!isBodyValid(handle)) {
        return Error{.code = ErrorCode::HandleResolutionFailed, .message = "Invalid BodyHandle"};
    }
    const auto& bodyInterface = impl_->physicsSystem_.GetBodyInterface();
    const auto rot = bodyInterface.GetRotation(impl_->slots_[handle.index].bodyId);
    return fromJolt(rot);
}

Result<void> PhysicsWorld::setRotation(BodyHandle handle, const Quat& rotation) {
    if (!isBodyValid(handle)) {
        return Error{.code = ErrorCode::HandleResolutionFailed, .message = "Invalid BodyHandle"};
    }
    auto& bodyInterface = impl_->physicsSystem_.GetBodyInterface();
    bodyInterface.SetRotation(impl_->slots_[handle.index].bodyId, toJolt(rotation),
                              JPH::EActivation::Activate);
    return Result<void>::success();
}

Result<Transform> PhysicsWorld::getTransform(BodyHandle handle) const {
    if (!isBodyValid(handle)) {
        return Error{.code = ErrorCode::HandleResolutionFailed, .message = "Invalid BodyHandle"};
    }
    const auto& bodyInterface = impl_->physicsSystem_.GetBodyInterface();
    const auto bodyId = impl_->slots_[handle.index].bodyId;
    JPH::RVec3 pos;
    JPH::Quat rot;
    bodyInterface.GetPositionAndRotation(bodyId, pos, rot);
    return Transform{fromJolt(pos), fromJolt(rot), Vec3::one()};
}

Result<void> PhysicsWorld::setTransform(BodyHandle handle, const Transform& transform) {
    if (!isBodyValid(handle)) {
        return Error{.code = ErrorCode::HandleResolutionFailed, .message = "Invalid BodyHandle"};
    }
    auto& bodyInterface = impl_->physicsSystem_.GetBodyInterface();
    bodyInterface.SetPositionAndRotation(impl_->slots_[handle.index].bodyId,
                                         toJolt(transform.position), toJolt(transform.rotation),
                                         JPH::EActivation::Activate);
    return Result<void>::success();
}

Result<Vec3> PhysicsWorld::getLinearVelocity(BodyHandle handle) const {
    if (!isBodyValid(handle)) {
        return Error{.code = ErrorCode::HandleResolutionFailed, .message = "Invalid BodyHandle"};
    }
    const auto& bodyInterface = impl_->physicsSystem_.GetBodyInterface();
    return fromJolt(bodyInterface.GetLinearVelocity(impl_->slots_[handle.index].bodyId));
}

Result<void> PhysicsWorld::setLinearVelocity(BodyHandle handle, const Vec3& velocity) {
    if (!isBodyValid(handle)) {
        return Error{.code = ErrorCode::HandleResolutionFailed, .message = "Invalid BodyHandle"};
    }
    auto& bodyInterface = impl_->physicsSystem_.GetBodyInterface();
    bodyInterface.SetLinearVelocity(impl_->slots_[handle.index].bodyId, toJolt(velocity));
    return Result<void>::success();
}

Result<Vec3> PhysicsWorld::getAngularVelocity(BodyHandle handle) const {
    if (!isBodyValid(handle)) {
        return Error{.code = ErrorCode::HandleResolutionFailed, .message = "Invalid BodyHandle"};
    }
    const auto& bodyInterface = impl_->physicsSystem_.GetBodyInterface();
    return fromJolt(bodyInterface.GetAngularVelocity(impl_->slots_[handle.index].bodyId));
}

Result<void> PhysicsWorld::setAngularVelocity(BodyHandle handle, const Vec3& velocity) {
    if (!isBodyValid(handle)) {
        return Error{.code = ErrorCode::HandleResolutionFailed, .message = "Invalid BodyHandle"};
    }
    auto& bodyInterface = impl_->physicsSystem_.GetBodyInterface();
    bodyInterface.SetAngularVelocity(impl_->slots_[handle.index].bodyId, toJolt(velocity));
    return Result<void>::success();
}

Result<void> PhysicsWorld::addForce(BodyHandle handle, const Vec3& force) {
    if (!isBodyValid(handle)) {
        return Error{.code = ErrorCode::HandleResolutionFailed, .message = "Invalid BodyHandle"};
    }
    auto& bodyInterface = impl_->physicsSystem_.GetBodyInterface();
    bodyInterface.AddForce(impl_->slots_[handle.index].bodyId, toJolt(force));
    return Result<void>::success();
}

Result<void> PhysicsWorld::addImpulse(BodyHandle handle, const Vec3& impulse) {
    if (!isBodyValid(handle)) {
        return Error{.code = ErrorCode::HandleResolutionFailed, .message = "Invalid BodyHandle"};
    }
    auto& bodyInterface = impl_->physicsSystem_.GetBodyInterface();
    bodyInterface.AddImpulse(impl_->slots_[handle.index].bodyId, toJolt(impulse));
    return Result<void>::success();
}

Result<MotionType> PhysicsWorld::getMotionType(BodyHandle handle) const {
    if (!isBodyValid(handle)) {
        return Error{.code = ErrorCode::HandleResolutionFailed, .message = "Invalid BodyHandle"};
    }
    const auto& bodyInterface = impl_->physicsSystem_.GetBodyInterface();
    return fromJolt(bodyInterface.GetMotionType(impl_->slots_[handle.index].bodyId));
}

Result<void> PhysicsWorld::setMotionType(BodyHandle handle, MotionType motionType) {
    if (!isBodyValid(handle)) {
        return Error{.code = ErrorCode::HandleResolutionFailed, .message = "Invalid BodyHandle"};
    }
    auto& bodyInterface = impl_->physicsSystem_.GetBodyInterface();
    const auto activation = (motionType == MotionType::Static) ? JPH::EActivation::DontActivate
                                                               : JPH::EActivation::Activate;
    bodyInterface.SetMotionType(impl_->slots_[handle.index].bodyId, toJolt(motionType), activation);
    return Result<void>::success();
}

RaycastHit PhysicsWorld::castRay(const RaycastQuery& query) const {
    RaycastHit result{};
    if (!impl_) {
        return result;
    }

    const JPH::RRayCast ray(toJolt(query.origin), toJolt(query.direction * query.maxDistance));
    JPH::RayCastResult hitResult;

    const bool hasHit = impl_->physicsSystem_.GetNarrowPhaseQuery().CastRay(ray, hitResult);
    if (hasHit) {
        result.hasHit = true;
        result.fraction = hitResult.mFraction;
        result.position =
            query.origin + (query.direction * (query.maxDistance * hitResult.mFraction));

        // Get body handle and surface normal
        JPH::BodyLockRead lock(impl_->physicsSystem_.GetBodyLockInterface(), hitResult.mBodyID);
        if (lock.Succeeded()) {
            const JPH::Body& body = lock.GetBody();
            result.body = impl_->resolveHandle(static_cast<uint32_t>(body.GetUserData()));
            result.normal = fromJolt(
                body.GetWorldSpaceSurfaceNormal(hitResult.mSubShapeID2, toJolt(result.position)));
        }
    }
    return result;
}

ShapeCastHit PhysicsWorld::castShape(const ShapeCastQuery& query) const {
    ShapeCastHit result{};
    if (!impl_) {
        return result;
    }

    auto shape = createJoltShape(query.shape);
    if (shape == nullptr) {
        return result;
    }

    const JPH::Mat44 startMat = JPH::Mat44::sRotationTranslation(
        toJolt(query.startTransform.rotation), toJolt(query.startTransform.position));
    const JPH::Vec3 castDir = toJolt(query.direction * query.maxDistance);

    JPH::RShapeCast shapeCast(shape, JPH::Vec3::sReplicate(1.0F), startMat, castDir);
    JPH::ShapeCastResult castResult;

    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    impl_->physicsSystem_.GetNarrowPhaseQuery().CastShape(shapeCast, JPH::ShapeCastSettings{},
                                                          JPH::RVec3::sZero(), collector);

    if (collector.HadHit()) {
        result.hasHit = true;
        result.fraction = collector.mHit.mFraction;
        result.contactPoint = fromJolt(collector.mHit.mContactPointOn2);
        result.contactNormal = fromJolt(-collector.mHit.mPenetrationAxis.Normalized());

        JPH::BodyLockRead lock(impl_->physicsSystem_.GetBodyLockInterface(),
                               collector.mHit.mBodyID2);
        if (lock.Succeeded()) {
            result.body = impl_->resolveHandle(static_cast<uint32_t>(lock.GetBody().GetUserData()));
        }
    }

    return result;
}

Result<uint32_t> PhysicsWorld::step(float deltaTime) {
    if (!impl_) {
        return Error{.code = ErrorCode::InvalidState, .message = "PhysicsWorld uninitialized"};
    }

    impl_->accumulatedTime_ += deltaTime;
    uint32_t substeps = 0;

    while (impl_->accumulatedTime_ >= impl_->settings_.fixedDeltaTime &&
           substeps < impl_->settings_.maxSubSteps) {
        impl_->physicsSystem_.Update(impl_->settings_.fixedDeltaTime, 1, &impl_->tempAllocator_,
                                     impl_->jobSystem_.get());
        impl_->accumulatedTime_ -= impl_->settings_.fixedDeltaTime;
        ++substeps;
    }

    // Clamp excess time if CPU fell too far behind to prevent death spiral
    const float maxAcc =
        impl_->settings_.fixedDeltaTime * static_cast<float>(impl_->settings_.maxSubSteps);
    if (impl_->accumulatedTime_ > maxAcc) {
        impl_->accumulatedTime_ = 0.0F;
    }

    impl_->contactListener_.updateSpan();
    return substeps;
}

float PhysicsWorld::getAccumulatedTime() const noexcept {
    return impl_ ? impl_->accumulatedTime_ : 0.0F;
}

void PhysicsWorld::resetAccumulator() noexcept {
    if (impl_) {
        impl_->accumulatedTime_ = 0.0F;
    }
}

std::span<const CollisionEvent> PhysicsWorld::getCollisionEvents() const noexcept {
    if (impl_) {
        return impl_->contactListener_.getEvents();
    }
    return {};
}

void PhysicsWorld::clearCollisionEvents() noexcept {
    if (impl_) {
        impl_->contactListener_.clearEvents();
    }
}

const PhysicsWorldSettings& PhysicsWorld::getSettings() const noexcept {
    static const PhysicsWorldSettings kDefaultSettings{};
    return impl_ ? impl_->settings_ : kDefaultSettings;
}

} // namespace eng::physics
