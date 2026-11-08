/// @file runtime_session.cpp
/// @brief Implementation of isolated runtime world lifecycle management.

#include "framework/runtime_session.h"

#include "anim/animation_components.h"
#include "core/log.h"
#include "framework/conductor.h"
#include "framework/scene_serializer.h"
#include "physics/physics_components.h"
#include "script/script_components.h"
#include "script/script_engine.h"
#include "world/world.h"

namespace eng::framework {

namespace {

/// Generic fallback Mode implementation for default runtime slots.
class GenericRuntimeMode final : public Mode {
public:
    explicit GenericRuntimeMode(std::string_view name) : name_(name) {}

    [[nodiscard]] std::string_view getName() const noexcept override { return name_; }

    [[nodiscard]] Result<void> enter(world::World&) override { return Result<void>::success(); }

    void update(world::World&, Duration) override {}

    [[nodiscard]] Result<void> exit(world::World&) override { return Result<void>::success(); }

    [[nodiscard]] Result<PreparationStatus> prepare(world::World&, const Budget&) override {
        return PreparationStatus::Ready;
    }

    [[nodiscard]] Result<void> commit(world::World&) override { return Result<void>::success(); }

    void cancelPrepare() override {}

    [[nodiscard]] ModeState getModeState() const noexcept override { return ModeState::Active; }

private:
    std::string name_;
};

} // namespace

struct RuntimeSession::Impl {
    RuntimeSessionConfig config;
    SimulationState state{SimulationState::Edit};

    world::World* editWorld{nullptr};
    Conductor* editConductor{nullptr};

    std::unique_ptr<physics::PhysicsWorld> runtimePhysicsWorld;
    std::unique_ptr<script::ScriptEngine> runtimeScriptEngine;
    std::unique_ptr<world::World> runtimeWorld;
    std::unique_ptr<Conductor> runtimeConductor;

    explicit Impl(const RuntimeSessionConfig& cfg) : config(cfg) {}

    ~Impl() { teardownRuntime(); }

    void teardownRuntime() noexcept {
        if (runtimeConductor) {
            runtimeConductor->shutdown();
            runtimeConductor.reset();
        }
        // Destroy runtime World first so ECS OnRemove observers can safely access
        // runtimePhysicsWorld and runtimeScriptEngine during entity destruction.
        runtimeWorld.reset();

        if (runtimeScriptEngine) {
            runtimeScriptEngine->unbindWorld();
            runtimeScriptEngine->shutdown();
            runtimeScriptEngine.reset();
        }
        runtimePhysicsWorld.reset();
    }

    Result<void> play() {
        if (state == SimulationState::Playing) {
            return Result<void>::failure(
                Error{ErrorCode::InvalidTransition, "Simulation is already playing"});
        }

        if (state == SimulationState::Paused) {
            state = SimulationState::Playing;
            ENG_LOG_INFO("RuntimeSession", "Simulation resumed to Playing state");
            return Result<void>::success();
        }

        if (editWorld == nullptr) {
            return Result<void>::failure(
                Error{ErrorCode::InvalidArgument, "Cannot start simulation: Edit World is null"});
        }

        // 1. Stage edit world state in memory (zero disk I/O)
        auto serRes = SceneSerializer::serializeToString(*editWorld);
        if (!serRes.hasValue()) {
            ENG_LOG_ERROR("RuntimeSession", "Failed to stage Edit World into memory for Play");
            return Result<void>::failure(serRes.error());
        }

        // 2. Clean up any previous runtime instance safely
        teardownRuntime();

        // 3. Allocate fresh isolated Runtime World
        runtimeWorld = std::make_unique<world::World>();

        // 4. Initialize runtime physics
        runtimePhysicsWorld = std::make_unique<physics::PhysicsWorld>(config.physicsSettings);
        physics::registerPhysicsComponents(*runtimeWorld);
        physics::registerPhysicsSystems(*runtimeWorld, *runtimePhysicsWorld);

        // 5. Initialize runtime animation
        anim::registerAnimationComponents(*runtimeWorld);

        // 6. Initialize runtime script engine
        runtimeScriptEngine = std::make_unique<script::ScriptEngine>();
        auto initScriptRes = runtimeScriptEngine->initialize();
        if (!initScriptRes.hasValue()) {
            ENG_LOG_ERROR("RuntimeSession", "Failed to initialize runtime script engine");
            teardownRuntime();
            return Result<void>::failure(
                Error{ErrorCode::InvalidState, "Failed to initialize runtime script engine"});
        }
        runtimeScriptEngine->bindWorld(runtimeWorld.get());
        (void)runtimeScriptEngine->executeString(R"(
function on_update(entity, dt)
    -- Default runtime entity update hook
end
)");
        script::registerScriptComponents(*runtimeWorld);
        script::registerScriptSystems(*runtimeWorld, *runtimeScriptEngine);

        // 7. Deserialize staged scene into isolated Runtime World
        auto desRes = SceneSerializer::deserializeFromString(*runtimeWorld, serRes.value(), true);
        if (!desRes.hasValue()) {
            ENG_LOG_ERROR("RuntimeSession", "Failed to deserialize scene into Runtime World");
            teardownRuntime();
            return Result<void>::failure(desRes.error());
        }

        // 8. Bind Jolt physics bodies for all entities with RigidBodyComponent
        auto entities = runtimeWorld->getAllEntities();
        for (auto h : entities) {
            auto ent = runtimeWorld->getRawWorld().entity(h.id);
            if (ent.has<physics::RigidBodyComponent>()) {
                auto* rb = ent.get_mut<physics::RigidBodyComponent>();
                physics::BodyDesc desc{};
                desc.motionType = rb->motionType;
                desc.collisionLayer = rb->collisionLayer;
                if (ent.has<physics::PhysicsTransformComponent>()) {
                    const auto* tc = ent.get<physics::PhysicsTransformComponent>();
                    desc.position = tc->transform.position;
                    desc.rotation = tc->transform.rotation;
                }
                if (ent.has<physics::ColliderComponent>()) {
                    const auto* cc = ent.get<physics::ColliderComponent>();
                    desc.collider = cc->desc;
                } else {
                    desc.collider = physics::ColliderDesc::makeBox(Vec3(0.5F, 0.5F, 0.5F));
                }
                auto bodyRes = runtimePhysicsWorld->createBody(desc);
                if (bodyRes.hasValue()) {
                    rb->bodyHandle = bodyRes.value();
                }
            }
        }

        // 9. Ensure continuous system group is active
        runtimeWorld->setSystemGroupActive(world::SystemGroup::Continuous, true);

        // 10. Create and initialize runtime Conductor
        std::unique_ptr<Mode> modeA;
        std::unique_ptr<Mode> modeB;
        if (config.modeFactory) {
            auto modes = config.modeFactory();
            modeA = std::move(modes.first);
            modeB = std::move(modes.second);
        } else {
            modeA = std::make_unique<GenericRuntimeMode>("State A");
            modeB = std::make_unique<GenericRuntimeMode>("State B");
        }

        runtimeConductor = std::make_unique<Conductor>(*runtimeWorld, std::move(modeA),
                                                       std::move(modeB), config.conductorConfig);
        auto condRes = runtimeConductor->initialize();
        if (!condRes.hasValue()) {
            ENG_LOG_ERROR("RuntimeSession", "Failed to initialize runtime Conductor");
            teardownRuntime();
            return Result<void>::failure(condRes.error());
        }

        state = SimulationState::Playing;
        ENG_LOG_INFO("RuntimeSession",
                     "Runtime simulation started successfully in isolated Runtime World");
        return Result<void>::success();
    }

    Result<void> pause() {
        if (state == SimulationState::Playing) {
            state = SimulationState::Paused;
            ENG_LOG_INFO("RuntimeSession", "Simulation paused");
            return Result<void>::success();
        }

        if (state == SimulationState::Paused) {
            return Result<void>::success();
        }

        return Result<void>::failure(
            Error{ErrorCode::InvalidTransition, "Cannot pause simulation when in edit mode"});
    }

    Result<void> resume() {
        if (state == SimulationState::Paused) {
            state = SimulationState::Playing;
            ENG_LOG_INFO("RuntimeSession", "Simulation resumed");
            return Result<void>::success();
        }

        return Result<void>::failure(
            Error{ErrorCode::InvalidTransition, "Cannot resume simulation: state is not Paused"});
    }

    Result<void> step(Duration delta) {
        if (state != SimulationState::Paused) {
            return Result<void>::failure(
                Error{ErrorCode::InvalidTransition, "Cannot step simulation: state is not Paused"});
        }

        if (runtimeConductor) {
            runtimeConductor->update(delta);
        }
        if (runtimeWorld) {
            runtimeWorld->progress(static_cast<float>(delta.toSeconds()));
        }

        ENG_LOG_INFO("RuntimeSession", "Stepped simulation by 1 frame while paused");
        return Result<void>::success();
    }

    Result<void> stop() {
        if (state == SimulationState::Playing || state == SimulationState::Paused) {
            state = SimulationState::Stopped;
            teardownRuntime();
            state = SimulationState::Edit;
            ENG_LOG_INFO("RuntimeSession",
                         "Simulation stopped; Runtime World destroyed; returned to Edit Mode");
            return Result<void>::success();
        }

        // Idempotent stop
        state = SimulationState::Edit;
        return Result<void>::success();
    }

    void update(Duration delta) {
        if (state == SimulationState::Playing) {
            if (runtimeConductor) {
                runtimeConductor->update(delta);
            }
            if (runtimeWorld) {
                runtimeWorld->progress(static_cast<float>(delta.toSeconds()));
            }
        }
    }
};

RuntimeSession::RuntimeSession(const RuntimeSessionConfig& config) noexcept
    : impl_(std::make_unique<Impl>(config)) {}

RuntimeSession::~RuntimeSession() noexcept = default;

RuntimeSession::RuntimeSession(RuntimeSession&&) noexcept = default;
RuntimeSession& RuntimeSession::operator=(RuntimeSession&&) noexcept = default;

void RuntimeSession::setEditWorld(world::World* world) noexcept {
    impl_->editWorld = world;
}

void RuntimeSession::setEditConductor(Conductor* conductor) noexcept {
    impl_->editConductor = conductor;
}

world::World* RuntimeSession::getEditWorld() noexcept {
    return impl_->editWorld;
}

const world::World* RuntimeSession::getEditWorld() const noexcept {
    return impl_->editWorld;
}

world::World* RuntimeSession::getRuntimeWorld() noexcept {
    return impl_->runtimeWorld.get();
}

const world::World* RuntimeSession::getRuntimeWorld() const noexcept {
    return impl_->runtimeWorld.get();
}

world::World* RuntimeSession::getActiveWorld() noexcept {
    return (impl_->state == SimulationState::Playing || impl_->state == SimulationState::Paused)
               ? impl_->runtimeWorld.get()
               : impl_->editWorld;
}

const world::World* RuntimeSession::getActiveWorld() const noexcept {
    return (impl_->state == SimulationState::Playing || impl_->state == SimulationState::Paused)
               ? impl_->runtimeWorld.get()
               : impl_->editWorld;
}

Conductor* RuntimeSession::getActiveConductor() noexcept {
    return (impl_->state == SimulationState::Playing || impl_->state == SimulationState::Paused)
               ? impl_->runtimeConductor.get()
               : impl_->editConductor;
}

const Conductor* RuntimeSession::getActiveConductor() const noexcept {
    return (impl_->state == SimulationState::Playing || impl_->state == SimulationState::Paused)
               ? impl_->runtimeConductor.get()
               : impl_->editConductor;
}

SimulationState RuntimeSession::getSimulationState() const noexcept {
    return impl_->state;
}

bool RuntimeSession::isSimulating() const noexcept {
    return impl_->state == SimulationState::Playing || impl_->state == SimulationState::Paused;
}

Result<void> RuntimeSession::play() {
    return impl_->play();
}

Result<void> RuntimeSession::pause() {
    return impl_->pause();
}

Result<void> RuntimeSession::resume() {
    return impl_->resume();
}

Result<void> RuntimeSession::step(Duration delta) {
    return impl_->step(delta);
}

Result<void> RuntimeSession::stop() {
    return impl_->stop();
}

void RuntimeSession::update(Duration delta) {
    impl_->update(delta);
}

} // namespace eng::framework
