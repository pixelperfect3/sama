// IK screenshot test.
// Scene: a programmatic 3-joint skeleton (shoulder -> elbow -> hand) with
// Two-Bone IK applied to reach a target position.  Small cubes are rendered
// at each solved joint position (colored differently) plus a cube at the
// target location.  This verifies that Two-Bone IK produces the expected
// visual result.

#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "GoldenCompare.h"
#include "ScreenshotFixture.h"
#include "engine/animation/IkSolvers.h"
#include "engine/animation/Pose.h"
#include "engine/animation/Skeleton.h"
#include "engine/math/Types.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ShaderUniforms.h"
#include "engine/rendering/ViewIds.h"

TEST_CASE("screenshot: IK two-bone solver", "[screenshot]")
{
    engine::screenshot::ScreenshotFixture fx;
    engine::rendering::ShaderUniforms uniforms;
    uniforms.init();

    bgfx::ProgramHandle prog = engine::rendering::loadPbrProgram();
    engine::rendering::RenderResources res;
    uint32_t meshId =
        res.addMesh(engine::rendering::buildMesh(engine::rendering::makeCubeMeshData()));
    const engine::rendering::Mesh& mesh = *res.getMesh(meshId);

    // -----------------------------------------------------------------------
    // Build a programmatic 3-joint skeleton:
    //   Joint 0 (shoulder): at origin
    //   Joint 1 (elbow):    offset (2, 0, 0) from shoulder
    //   Joint 2 (hand):     offset (2, 0, 0) from elbow
    // Total chain length = 4 units along +X.
    // -----------------------------------------------------------------------

    engine::animation::Skeleton skeleton;
    skeleton.joints.resize(3);

    // Joint 0: root (shoulder)
    skeleton.joints[0].parentIndex = -1;
    skeleton.joints[0].inverseBindMatrix = glm::inverse(glm::mat4(1.0f));

    // Joint 1: elbow, child of shoulder
    skeleton.joints[1].parentIndex = 0;
    skeleton.joints[1].inverseBindMatrix =
        glm::inverse(glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f)));

    // Joint 2: hand, child of elbow
    skeleton.joints[2].parentIndex = 1;
    skeleton.joints[2].inverseBindMatrix =
        glm::inverse(glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 0.0f, 0.0f)));

    // -----------------------------------------------------------------------
    // Set up the initial bind pose (T-pose along +X).
    // -----------------------------------------------------------------------

    engine::animation::Pose pose;
    pose.jointPoses.resize(3);

    // Shoulder at origin
    pose.jointPoses[0].position = glm::vec3(0.0f, 0.0f, 0.0f);
    pose.jointPoses[0].rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    pose.jointPoses[0].scale = glm::vec3(1.0f);

    // Elbow offset from shoulder
    pose.jointPoses[1].position = glm::vec3(2.0f, 0.0f, 0.0f);
    pose.jointPoses[1].rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    pose.jointPoses[1].scale = glm::vec3(1.0f);

    // Hand offset from elbow
    pose.jointPoses[2].position = glm::vec3(2.0f, 0.0f, 0.0f);
    pose.jointPoses[2].rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    pose.jointPoses[2].scale = glm::vec3(1.0f);

    // -----------------------------------------------------------------------
    // Compute world positions and apply Two-Bone IK.
    // Target: hand should reach (2, 2, 0) — within reach (total length 4).
    // Pole vector hint: bend toward +Z to break symmetry.
    // -----------------------------------------------------------------------

    glm::vec3 worldPositions[3];
    engine::animation::computeWorldPositions(skeleton, pose, worldPositions);

    glm::vec3 targetPos(2.0f, 2.0f, 0.0f);
    glm::vec3 poleVector(0.0f, 0.0f, 1.0f);

    engine::animation::solveTwoBone(skeleton, pose, worldPositions,
                                    0,  // root (shoulder)
                                    1,  // mid (elbow)
                                    2,  // tip (hand)
                                    targetPos, poleVector);

    // -----------------------------------------------------------------------
    // Render cubes at solved joint positions + target position.
    // -----------------------------------------------------------------------

    // Camera looking at the arm from front-ish angle.
    auto view = glm::lookAt(glm::vec3(2, 2, 8), glm::vec3(2, 1, 0), glm::vec3(0, 1, 0));
    auto proj = glm::perspective(glm::radians(45.0f), static_cast<float>(fx.width()) / fx.height(),
                                 0.1f, 50.0f);

    engine::rendering::RenderPass(engine::rendering::kViewOpaque)
        .framebuffer(fx.captureFb())
        .rect(0, 0, fx.width(), fx.height())
        .clearColorAndDepth(0x202030ff)
        .transform(view, proj);

    // Directional light from upper-right.
    glm::vec3 lightDir = glm::normalize(glm::vec3(1.0f, 2.0f, 1.0f));
    float lightData[8] = {lightDir.x, lightDir.y, lightDir.z, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f};

    // Helper lambda: draw a small cube at a given world position with a given color.
    auto drawCube = [&](const glm::vec3& pos, float cubeScale, const float* matData)
    {
        bgfx::setUniform(uniforms.u_material, matData, 2);
        bgfx::setUniform(uniforms.u_dirLight, lightData, 2);
        bgfx::setTexture(0, uniforms.s_albedo, fx.whiteTex());
        bgfx::setTexture(1, uniforms.s_normal, fx.neutralNormalTex());
        bgfx::setTexture(2, uniforms.s_orm, fx.whiteTex());

        auto model = glm::translate(glm::mat4(1.0f), pos);
        model = glm::scale(model, glm::vec3(cubeScale));
        float mtx[16] = {};
        memcpy(mtx, &model[0][0], sizeof(float) * 16);

        bgfx::setTransform(mtx);
        bgfx::setVertexBuffer(0, mesh.positionVbh);
        bgfx::setVertexBuffer(1, mesh.surfaceVbh);
        bgfx::setIndexBuffer(mesh.ibh);
        bgfx::setState(BGFX_STATE_DEFAULT);
        bgfx::submit(engine::rendering::kViewOpaque, prog);
    };

    // Joint 0 (shoulder) — red
    float matShoulder[8] = {0.9f, 0.2f, 0.2f, 0.4f, 0.0f, 0.0f, 0.0f, 0.0f};
    drawCube(worldPositions[0], 0.25f, matShoulder);

    // Joint 1 (elbow) — green
    float matElbow[8] = {0.2f, 0.9f, 0.2f, 0.4f, 0.0f, 0.0f, 0.0f, 0.0f};
    drawCube(worldPositions[1], 0.25f, matElbow);

    // Joint 2 (hand) — blue
    float matHand[8] = {0.2f, 0.2f, 0.9f, 0.4f, 0.0f, 0.0f, 0.0f, 0.0f};
    drawCube(worldPositions[2], 0.25f, matHand);

    // Target position — yellow wireframe-ish (smaller cube)
    float matTarget[8] = {0.9f, 0.9f, 0.1f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f};
    drawCube(targetPos, 0.15f, matTarget);

    auto pixels = fx.captureFrame();

    if (bgfx::isValid(prog))
        bgfx::destroy(prog);
    res.destroyAll();
    uniforms.destroy();

    REQUIRE(
        engine::screenshot::compareOrUpdateGolden("ik_two_bone", pixels, fx.width(), fx.height()));
}
