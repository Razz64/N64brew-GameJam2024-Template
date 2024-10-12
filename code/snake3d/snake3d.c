#include <libdragon.h>
#include "../../minigame.h"
#include "../../core.h"
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>
#include <t3d/t3ddebug.h>

const MinigameDef minigame_def = {
    .gamename = "Snake3D",
    .developername = "HailToDodongo",
    .description = "This is a porting of one of the Tiny3D examples, to show how to "
                   "integrate Tiny3D in minigame",
    .instructions = "Press A to attack. Last snake slithering wins!"
};

#define HITBOX_RADIUS       10.f

#define ATTACK_OFFSET       10.f
#define ATTACK_RADIUS       5.f

#define ATTACK_TIME_START   0.333f
#define ATTACK_TIME_END     0.4f

#define WIN_DELAY           5.0f

/**
 * Example project showcasing the usage of the animation system.
 * This includes instancing animations, blending animations, and controlling playback.
 */

surface_t *depthBuffer;
T3DViewport viewport;
rdpq_font_t *font;
T3DMat4FP* mapMatFP;
rspq_block_t *dplMap;
T3DModel *model;
T3DModel *modelShadow;
T3DModel *modelMap;
T3DVec3 camPos;
T3DVec3 camTarget;
T3DVec3 lightDirVec;
xm64player_t music;


typedef struct
{
  T3DMat4FP* modelMatFP;
  rspq_block_t *dplSnake;
  T3DAnim animAttack;
  T3DAnim animWalk;
  T3DAnim animIdle;
  T3DSkeleton skelBlend;
  T3DSkeleton skel;
  T3DVec3 moveDir;
  T3DVec3 playerPos;
  float rotY;
  float currSpeed;
  float animBlend;
  bool isAttack;
  bool isAlive;
  float attackTimer;
} player_data;

player_data players[MAXPLAYERS];

bool isEnding;
float endTimer;
PlyNum winner;

rspq_syncpoint_t syncPoint;

void player_init(player_data *player, color_t color, T3DVec3 position, float rotation)
{
  player->modelMatFP = malloc_uncached(sizeof(T3DMat4FP));

  player->moveDir = (T3DVec3){{0,0,0}};
  player->playerPos = position;

  // First instantiate skeletons, they will be used to draw models in a specific pose
  // And serve as the target for animations to modify
  player->skel = t3d_skeleton_create(model);
  player->skelBlend = t3d_skeleton_clone(&player->skel, false); // optimized for blending, has no matrices

  // Now create animation instances (by name), the data in 'model' is fixed,
  // whereas 'anim' contains all the runtime data.
  // Note that tiny3d internally keeps no track of animations, it's up to the user to manage and play them.
  player->animIdle = t3d_anim_create(model, "Snake_Idle");
  t3d_anim_attach(&player->animIdle, &player->skel); // tells the animation which skeleton to modify

  player->animWalk = t3d_anim_create(model, "Snake_Walk");
  t3d_anim_attach(&player->animWalk, &player->skelBlend);

  // multiple animations can attach to the same skeleton, this will NOT perform any blending
  // rather the last animation that updates "wins", this can be useful if multiple animations touch different bones
  player->animAttack = t3d_anim_create(model, "Snake_Attack");
  t3d_anim_set_looping(&player->animAttack, false); // don't loop this animation
  t3d_anim_set_playing(&player->animAttack, false); // start in a paused state
  t3d_anim_attach(&player->animAttack, &player->skel);

  rspq_block_begin();
    t3d_matrix_push(player->modelMatFP);
    rdpq_set_prim_color(color);
    t3d_model_draw_skinned(model, &player->skel); // as in the last example, draw skinned with the main skeleton

    rdpq_set_prim_color(RGBA32(0, 0, 0, 120));
    t3d_model_draw(modelShadow);
    t3d_matrix_pop(1);
  player->dplSnake = rspq_block_end();

  player->rotY = rotation;
  player->currSpeed = 0.0f;
  player->animBlend = 0.0f;
  player->isAttack = false;
  player->isAlive = true;

  xm64player_open(&music, "rom:/snake3d/bottled_bubbles.xm64");
}

void minigame_init(void)
{
  display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS);
  depthBuffer = display_get_zbuf();

  t3d_init((T3DInitParams){});
  font = rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO);
  rdpq_text_register_font(FONT_BUILTIN_DEBUG_MONO, font);
  viewport = t3d_viewport_create();

  mapMatFP = malloc_uncached(sizeof(T3DMat4FP));
  t3d_mat4fp_from_srt_euler(mapMatFP, (float[3]){0.3f, 0.3f, 0.3f}, (float[3]){0, 0, 0}, (float[3]){0, 0, -10});

  camPos = (T3DVec3){{0, 120.0f, 100.0f}};
  camTarget = (T3DVec3){{0, 0, 50}};

  lightDirVec = (T3DVec3){{1.0f, 1.0f, 1.0f}};
  t3d_vec3_norm(&lightDirVec);

  modelMap = t3d_model_load("rom:/snake3d/map.t3dm");
  modelShadow = t3d_model_load("rom:/snake3d/shadow.t3dm");

  // Model Credits: Quaternius (CC0) https://quaternius.com/packs/easyenemy.html
  model = t3d_model_load("rom:/snake3d/snake.t3dm");

  rspq_block_begin();
    t3d_matrix_push(mapMatFP);
    rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
    t3d_model_draw(modelMap);
    t3d_matrix_pop(1);
  dplMap = rspq_block_end();

  uint32_t colors[] = {
    PLAYERCOLOR_1,
    PLAYERCOLOR_2,
    PLAYERCOLOR_3,
    PLAYERCOLOR_4,
  };

  T3DVec3 start_positions[] = {
    (T3DVec3){{-100,0.15f,0}},
    (T3DVec3){{0,0.15f,-100}},
    (T3DVec3){{100,0.15f,0}},
    (T3DVec3){{0,0.15f,100}},
  };

  float start_rotations[] = {
    M_PI/2,
    0,
    3*M_PI/2,
    M_PI
  };

  for (size_t i = 0; i < MAXPLAYERS; i++)
  {
    player_init(&players[i], color_from_packed32(colors[i]<<8), start_positions[i], start_rotations[i]);
  }

  syncPoint = 0;
  xm64player_play(&music, 0);
}

void player_do_damage(player_data *player)
{
  if (!player->isAlive) {
    // Prevent edge cases
    return;
  }

  float s, c;
  fm_sincosf(player->rotY, &s, &c);
  float attack_pos[] = {
    player->playerPos.v[0] + s * ATTACK_OFFSET,
    player->playerPos.v[2] + c * ATTACK_OFFSET,
  };

  for (size_t i = 0; i < MAXPLAYERS; i++)
  {
    player_data *other_player = &players[i];
    if (other_player == player || !other_player->isAlive) continue;

    float pos_diff[] = {
      other_player->playerPos.v[0] - attack_pos[0],
      other_player->playerPos.v[2] - attack_pos[1],
    };

    float distance = sqrtf(pos_diff[0]*pos_diff[0] + pos_diff[1]*pos_diff[1]);

    if (distance < (ATTACK_RADIUS + HITBOX_RADIUS)) {
      other_player->isAlive = false;
    }
  }
}

void player_fixedloop(player_data *player, float deltaTime, joypad_port_t port, bool is_human)
{
  float speed = 0.0f;
  T3DVec3 newDir = {0};

  if (player->isAlive)
  {
    if (is_human)
    {
      joypad_inputs_t joypad = joypad_get_inputs(port);

      newDir.v[0] = (float)joypad.stick_x * 0.05f;
      newDir.v[2] = -(float)joypad.stick_y * 0.05f;
      speed = sqrtf(t3d_vec3_len2(&newDir));
    }
    else
    {
      player->rotY += 0.1f;
    }
  }

  // Player movement
  if(speed > 0.15f && !player->isAttack) {
    newDir.v[0] /= speed;
    newDir.v[2] /= speed;
    player->moveDir = newDir;

    float newAngle = atan2f(player->moveDir.v[0], player->moveDir.v[2]);
    player->rotY = t3d_lerp_angle(player->rotY, newAngle, 0.5f);
    player->currSpeed = t3d_lerp(player->currSpeed, speed * 0.3f, 0.15f);
  } else {
    player->currSpeed *= 0.64f;
  }

  // use blend based on speed for smooth transitions
  player->animBlend = player->currSpeed / 0.51f;
  if(player->animBlend > 1.0f)player->animBlend = 1.0f;

  // move player...
  player->playerPos.v[0] += player->moveDir.v[0] * player->currSpeed;
  player->playerPos.v[2] += player->moveDir.v[2] * player->currSpeed;
  // ...and limit position inside the box
  const float BOX_SIZE = 140.0f;
  if(player->playerPos.v[0] < -BOX_SIZE)player->playerPos.v[0] = -BOX_SIZE;
  if(player->playerPos.v[0] >  BOX_SIZE)player->playerPos.v[0] =  BOX_SIZE;
  if(player->playerPos.v[2] < -BOX_SIZE)player->playerPos.v[2] = -BOX_SIZE;
  if(player->playerPos.v[2] >  BOX_SIZE)player->playerPos.v[2] =  BOX_SIZE;

  if (player->isAttack) {
    player->attackTimer += deltaTime;
    if (player->attackTimer > ATTACK_TIME_START && player->attackTimer < ATTACK_TIME_END) {
      player_do_damage(player);
    }
  }
}

void player_loop(player_data *player, float deltaTime, joypad_port_t port, bool is_human)
{
  if (is_human && player->isAlive)
  {
    joypad_buttons_t btn = joypad_get_buttons_pressed(port);

    if (btn.start) minigame_end();

    // Player Attack
    if((btn.a || btn.b) && !player->animAttack.isPlaying) {
      t3d_anim_set_playing(&player->animAttack, true);
      t3d_anim_set_time(&player->animAttack, 0.0f);
      player->isAttack = true;
      player->attackTimer = 0;
    }
  }
  
  // Update the animation and modify the skeleton, this will however NOT recalculate the matrices
  t3d_anim_update(&player->animIdle, deltaTime);
  t3d_anim_set_speed(&player->animWalk, player->animBlend + 0.15f);
  t3d_anim_update(&player->animWalk, deltaTime);

  if(player->isAttack) {
    t3d_anim_update(&player->animAttack, deltaTime); // attack animation now overrides the idle one
    if(!player->animAttack.isPlaying)player->isAttack = false;
  }

  // We now blend the walk animation with the idle/attack one
  t3d_skeleton_blend(&player->skel, &player->skel, &player->skelBlend, player->animBlend);

  if(syncPoint)rspq_syncpoint_wait(syncPoint); // wait for the RSP to process the previous frame

  // Now recalc. the matrices, this will cause any model referencing them to use the new pose
  t3d_skeleton_update(&player->skel);

  // Update player matrix
  t3d_mat4fp_from_srt_euler(player->modelMatFP,
    (float[3]){0.125f, 0.125f, 0.125f},
    (float[3]){0.0f, -player->rotY, 0},
    player->playerPos.v
  );
}

void player_draw(player_data *player)
{
  if (player->isAlive) {
    rspq_block_run(player->dplSnake);
  }
}

void minigame_fixedloop(float deltaTime)
{
  uint32_t playercount = core_get_playercount();
  for (size_t i = 0; i < MAXPLAYERS; i++)
  {
    player_fixedloop(&players[i], deltaTime, core_get_playercontroller(i), i < playercount);
  }

  if (!isEnding) {
    // Determine if a player has won
    uint32_t alivePlayers = 0;
    PlyNum lastPlayer = 0;
    for (size_t i = 0; i < MAXPLAYERS; i++)
    {
      if (players[i].isAlive)
      {
        alivePlayers++;
        lastPlayer = i;
      }
    }
    
    if (alivePlayers == 1) {
      isEnding = true;
      winner = lastPlayer;
    }
  } else {
    endTimer += deltaTime;
    if (endTimer > WIN_DELAY) {
      core_set_winner(winner);
      minigame_end();
    }
  }
}

void minigame_loop(float deltaTime)
{
  uint8_t colorAmbient[4] = {0xAA, 0xAA, 0xAA, 0xFF};
  uint8_t colorDir[4]     = {0xFF, 0xAA, 0xAA, 0xFF};

  t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(90.0f), 20.0f, 160.0f);
  t3d_viewport_look_at(&viewport, &camPos, &camTarget, &(T3DVec3){{0,1,0}});

  uint32_t playercount = core_get_playercount();
  for (size_t i = 0; i < MAXPLAYERS; i++)
  {
    player_loop(&players[i], deltaTime, core_get_playercontroller(i), i < playercount);
  }

  // ======== Draw (3D) ======== //
  rdpq_attach(display_get(), depthBuffer);
  t3d_frame_start();
  t3d_viewport_attach(&viewport);

  t3d_screen_clear_color(RGBA32(224, 180, 96, 0xFF));
  t3d_screen_clear_depth();

  t3d_light_set_ambient(colorAmbient);
  t3d_light_set_directional(0, colorDir, &lightDirVec);
  t3d_light_set_count(1);

  rspq_block_run(dplMap);
  for (size_t i = 0; i < MAXPLAYERS; i++)
  {
    player_draw(&players[i]);
  }

  syncPoint = rspq_syncpoint_new();

  if (isEnding) {
    rdpq_sync_pipe(); // Hardware crashes otherwise
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 120, 100, "Player %d wins!", winner+1);
  }

  rdpq_detach_show();
}

void player_cleanup(player_data *player)
{
  rspq_block_free(player->dplSnake);

  t3d_skeleton_destroy(&player->skel);
  t3d_skeleton_destroy(&player->skelBlend);

  t3d_anim_destroy(&player->animIdle);
  t3d_anim_destroy(&player->animWalk);
  t3d_anim_destroy(&player->animAttack);

  free_uncached(player->modelMatFP);
  xm64player_stop(&music);
}

void minigame_cleanup(void)
{
  for (size_t i = 0; i < MAXPLAYERS; i++)
  {
    player_cleanup(&players[i]);
  }

  xm64player_close(&music);
  rspq_block_free(dplMap);

  t3d_model_free(model);
  t3d_model_free(modelMap);
  t3d_model_free(modelShadow);

  free_uncached(mapMatFP);

  rdpq_text_unregister_font(FONT_BUILTIN_DEBUG_MONO);
  rdpq_font_free(font);
  t3d_destroy();

  display_close();
}