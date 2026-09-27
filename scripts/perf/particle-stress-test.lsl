// Vulkanstorm particle stress test. Put in ROOT of a dedicated, nonphysical
// linkset with exactly 16 child prims named PS_EMITTER. Compile as Mono.
// Owner chat: /73043 run billboard spread ; /73043 stop ; /73043 help
// Runs change emitter child positions/rotations. See particle-stress-test.md.
integer CHANNEL = 73043;
string EMITTER_NAME = "PS_EMITTER";
float WARMUP = 15.0;
float MEASURE = 30.0;
float DRAIN = 10.0;
list LOADS = [1, 4, 8, 16];
list emitters;
string mode = "billboard";
string layout = "spread";
integer running;
integer level;
integer active;
integer phase; // 0 warm-up, 1 measurement, 2 drain
float deadline;
float stageStart;
string runID;

say(string message)
{
    llOwnerSay("PSBENCH," + llGetTimestamp() + "," + runID + ","
        + mode + "," + layout + "," + (string)active + "," + message);
}

scan()
{
    emitters = [];
    integer link;
    integer count = llGetObjectPrimCount(llGetKey());
    for (link = 2; link <= count; ++link)
        if (llGetLinkName(link) == EMITTER_NAME) emitters += [link];
}

clearParticles()
{
    integer i;
    for (i = 0; i < llGetListLength(emitters); ++i)
        llLinkParticleSystem(llList2Integer(emitters, i), []);
}

stopRun(string reason)
{
    clearParticles();
    running = FALSE;
    llSetTimerEvent(0.0);
    say(reason + "; allow 10 seconds for existing particles to expire");
}

vector position(integer i, float elapsed)
{
    float spacing = 4.0;
    if (layout == "overlap") spacing = 0.15;
    vector p = <((i % 4) - 1.5) * spacing,
                ((i / 4) - 1.5) * spacing, 2.0>;
    if (mode == "ribbon")
    {
        float angle = elapsed * 1.5 + (float)i * TWO_PI / 16.0;
        p += <0.75 * llCos(angle), 0.75 * llSin(angle), 0.0>;
    }
    return p;
}

place(integer count, float elapsed)
{
    integer i;
    list params = [];
    for (i = 0; i < count; ++i)
        params += [PRIM_LINK_TARGET, llList2Integer(emitters, i),
            PRIM_POS_LOCAL, position(i, elapsed), PRIM_ROT_LOCAL, ZERO_ROTATION];
    llSetLinkPrimitiveParamsFast(LINK_THIS, params);
}

list rules()
{
    integer flags = PSYS_PART_INTERP_COLOR_MASK | PSYS_PART_INTERP_SCALE_MASK;
    integer pattern = PSYS_SRC_PATTERN_EXPLODE;
    integer burst = 8;
    float rate = 0.05;
    float life = 8.0;
    float glow = 0.0;
    vector scale = <0.25, 0.25, 0.0>;
    vector accel = ZERO_VECTOR;
    if (mode == "velocity")
    {
        flags = flags | PSYS_PART_FOLLOW_VELOCITY_MASK;
        scale = <0.125, 0.5, 0.0>;
    }
    else if (mode == "ribbon")
    {
        flags = flags | PSYS_PART_RIBBON_MASK;
        pattern = PSYS_SRC_PATTERN_DROP;
        burst = 1; // Avoid coincident zero-length segments in each burst.
        rate = 0.02;
        accel = <0.1, 0.0, 0.0>;
    }
    else if (mode == "glow")
    {
        flags = flags | PSYS_PART_EMISSIVE_MASK;
        glow = 0.15;
        scale = <1.0, 1.0, 0.0>;
    }
    else if (mode == "churn")
    {
        life = 1.0;
        burst = 64; // Same nominal live population; eight times the births.
    }
    return [
        PSYS_PART_FLAGS, flags,
        PSYS_SRC_PATTERN, pattern,
        PSYS_SRC_TEXTURE, TEXTURE_BLANK,
        PSYS_PART_START_COLOR, <0.2, 0.7, 1.0>,
        PSYS_PART_END_COLOR, <1.0, 0.3, 0.1>,
        PSYS_PART_START_ALPHA, 0.65,
        PSYS_PART_END_ALPHA, 0.05,
        PSYS_PART_START_SCALE, scale,
        PSYS_PART_END_SCALE, scale * 0.5,
        PSYS_PART_START_GLOW, glow,
        PSYS_PART_END_GLOW, 0.0,
        PSYS_PART_MAX_AGE, life,
        PSYS_SRC_MAX_AGE, 60.0, // Finite emitter lifetime if controller stops.
        PSYS_SRC_BURST_RATE, rate,
        PSYS_SRC_BURST_PART_COUNT, burst,
        PSYS_SRC_BURST_RADIUS, 0.0,
        PSYS_SRC_BURST_SPEED_MIN, 0.25,
        PSYS_SRC_BURST_SPEED_MAX, 0.25,
        PSYS_SRC_ACCEL, accel,
        PSYS_SRC_OMEGA, ZERO_VECTOR];
}

beginStage()
{
    active = llList2Integer(LOADS, level);
    place(16, 0.0);
    list particleRules = rules();
    integer i;
    for (i = 0; i < active; ++i)
        llLinkParticleSystem(llList2Integer(emitters, i), particleRules);
    phase = 0;
    stageStart = llGetTime();
    deadline = stageStart + WARMUP;
    integer nominal = active * 1280;
    if (mode == "ribbon") nominal = active * 400;
    say("WARMUP; nominal_live=" + (string)nominal
        + "; nominal is a request estimate, not an observed count");
}

help()
{
    llOwnerSay("/73043 run MODE LAYOUT\nModes: billboard velocity ribbon glow churn"
        + "\nLayouts: spread overlap\nExample: /73043 run billboard spread"
        + "\n/73043 stop | /73043 status | /73043 help"
        + "\nTouch controller to stop. Each run: 4 stages, about 230 seconds."
        + "\nRequires 16 children named PS_EMITTER; positions will be changed.");
}

default
{
    state_entry()
    {
        scan();
        clearParticles();
        llListen(CHANNEL, "", llGetOwner(), "");
        help();
    }
    on_rez(integer parameter) { llResetScript(); }
    changed(integer change)
    {
        if (change & (CHANGED_OWNER | CHANGED_LINK))
        {
            // Re-scan before clearing so relinking cannot target stale link IDs.
            scan();
            stopRun("ABORT: owner or linkset changed");
            llResetScript();
        }
    }
    touch_start(integer count)
    {
        integer i;
        for (i = 0; i < count; ++i)
            if (llDetectedKey(i) == llGetOwner()) stopRun("STOP: owner touch");
    }
    listen(integer channel, string name, key id, string message)
    {
        list words = llParseString2List(llToLower(llStringTrim(message, STRING_TRIM)), [" "], []);
        string command = llList2String(words, 0);
        if (command == "stop") stopRun("STOP: owner command");
        else if (command == "status")
            say("STATUS; running=" + (string)running + "; phase=" + (string)phase);
        else if (command == "run")
        {
            if (running)
            {
                llOwnerSay("Already running. Stop, then allow 10 seconds to drain before restarting.");
                return;
            }
            string newMode = llList2String(words, 1);
            string newLayout = llList2String(words, 2);
            if (llGetListLength(words) != 3
                || llListFindList(["billboard", "velocity", "ribbon", "glow", "churn"], [newMode]) < 0
                || llListFindList(["spread", "overlap"], [newLayout]) < 0)
            {
                help();
                return;
            }
            scan();
            if (llGetListLength(emitters) != 16 || llGetLinkNumber() != 1
                || llGetAttached() != 0 || llGetStatus(STATUS_PHYSICS))
            {
                llOwnerSay("Use a rezzed, nonphysical linkset: controller in root, exactly 16 PS_EMITTER children.");
                return;
            }
            mode = newMode;
            layout = newLayout;
            runID = llGetTimestamp();
            level = 0;
            running = TRUE;
            clearParticles();
            // Initial drain also protects a quick stop/restart.
            active = 0;
            phase = 2;
            deadline = llGetTime() + DRAIN;
            say("INITIAL_DRAIN");
            llSetTimerEvent(0.25);
        }
        else help();
    }
    timer()
    {
        float now = llGetTime();
        if (phase != 2 && now - stageStart >= 60.0)
        {
            stopRun("INVALID: timer delay exceeded emitter lifetime; repeat run");
            return;
        }
        if (mode == "ribbon" && phase != 2) place(active, now - stageStart);
        if (now < deadline) return;
        if (phase == 0)
        {
            phase = 1;
            deadline = now + MEASURE;
            say("MEASURE_BEGIN; window_seconds=" + (string)MEASURE);
        }
        else if (phase == 1)
        {
            say("MEASURE_END");
            clearParticles();
            phase = 2;
            deadline = now + DRAIN;
            ++level;
            say("DRAIN");
        }
        else if (level < llGetListLength(LOADS)) beginStage();
        else stopRun("COMPLETE");
    }
}
