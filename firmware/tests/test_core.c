#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "wire.h"

static char output[512];
static int pulses;
static int servo;
static bool pump;
static bool forward;

void io_send(const char *line)
{
    assert(strlen(output) + strlen(line) + 1 < sizeof(output));
    strcat(output, line);
    strcat(output, "\n");
}

void io_servo(int angle) { servo = angle; }
void io_direction(bool fwd) { forward = fwd; }
void io_step(bool high) { pulses += high ? 1 : 0; }
void io_pump(bool active) { pump = active; }

static void expect(const char *expected)
{
    if (strcmp(output, expected) != 0) {
        fprintf(stderr, "Expected: %sActual: %s\n", expected, output);
        assert(false);
    }
    output[0] = '\0';
}

#ifdef CONTROLLER
static void synchronized(const char *state)
{
    app_init();
    app_tick(1);
    expect("STATUS?\n");
    app_receive(state, 2);
}

static void check_thresholds(void)
{
    synchronized("STATE:0,0,0,0");
    app_receive("SENS:280,60,75,60,40,50,210", 3);
    app_tick(3);
    expect("SERV:090;\n");

    app_receive("STATE:90,0,0,0", 400);
    app_receive("OK", 400);
    app_tick(400);
    expect("STEP:FWD,200;\n");

    app_tick(6400);
    expect("STATUS?\n");
    app_receive("STATE:90,200,0,0", 6401);
    app_receive("OK", 6401);
    app_receive("SENS:260,69,75,30,40,50,210", 6401);
    app_tick(6401);
    expect("");

    app_receive("SENS:250,70,55,28,40,50,210", 6402);
    app_tick(6402);
    expect("SERV:000;\n");
    app_receive("STATE:0,200,0,0", 6802);
    app_receive("OK", 6802);
    app_tick(6802);
    expect("STEP:REV,200;\n");
}

static void check_irrigation(void)
{
    synchronized("STATE:0,0,0,0");
    app_receive("SENS:245,62,55,55,20,10,210", 3);
    app_tick(3);
    expect("INFO:IRRIGATION,WATER\n");

    app_receive("SENS:245,62,55,55,20,5,210", 4);
    app_tick(4);
    expect("");

    app_receive("SENS:245,62,55,55,NA,65,210", 5);
    app_tick(5);
    expect("INFO:IRRIGATION,SENSOR\n");

    app_receive("SENS:245,62,55,55,20,101,210", 6);
    app_tick(6);
    expect("");

    app_receive("SENS:245,62,55,55,20,65,210", 7);
    app_tick(7);
    expect("INFO:IRRIGATION,READY\nPUMP:003;\n");
    app_receive("STATE:0,0,1,1", 7);
    app_receive("STATE:0,0,0,0", 3005);
    app_receive("OK", 3005);
    app_tick(3005);
    expect("");

    app_tick(18000);
    expect("STATUS?\n");
    app_receive("STATE:0,0,0,0", 18001);
    app_receive("SENS:245,62,55,55,20,65,210", 18001);
    app_tick(18004);
    expect("");
    app_tick(18005);
    expect("PUMP:003;\n");
}

static void check_stale_telemetry(void)
{
    synchronized("STATE:0,0,0,0");
    app_receive("SENS:245,62,55,55,20,65,210", 3);
    app_tick(5000);
    expect("STATUS?\n");
    app_receive("STATE:0,0,0,0", 5000);
    app_tick(5001);
    expect("");
}

static void check_angle_diagnostic(void)
{
    app_init();
    app_receive("DIAG:ANGLE", 0);
    app_tick(0);
    expect("STATUS?\n");

    app_receive("STATE:0,0,1,1", 1);
    app_tick(2);
    expect("");
    app_tick(2001);
    expect("STATUS?\n");

    app_receive("STATE:0,0,0,0", 2002);
    app_tick(2002);
    expect("SERV:181;\n");
    app_tick(2003);
    expect("");

    app_receive("ERROR", 2004);
    app_tick(2004);
    expect("STATUS?\n");
    app_receive("STATE:0,0,0,0", 2005);
    app_receive("DIAG:STEP", 2005);
    app_tick(2005);
    expect("");
}
#else
static void check_actuator_commands(void)
{
    app_init();
    FrameReader reader = {0};
    const char *query = "STATUS?\r\n";
    for (const char *byte = query; *byte; ++byte) {
        frame_byte(&reader, (uint8_t)*byte, 1);
    }
    expect("STATE:0,0,0,0\n");
    for (unsigned i = 0; i < 100; ++i) {
        frame_byte(&reader, 'A', 1);
    }
    frame_byte(&reader, '\n', 1);
    expect("ERROR\n");
    app_receive("SERV:181;", 1);
    app_receive("STEP:REV,1;", 1);
    app_receive("STEP:FWD,0;", 1);
    app_receive("PUMP:999;", 1);
    app_receive("SERV:+90;", 1);
    app_receive("SERV:090;garbage", 1);
    app_receive("SERV:PUMP:003;", 1);
    app_receive("STEP:FWD,PUMP:003;", 1);
    expect("ERROR\nERROR\nERROR\nERROR\nERROR\nERROR\nERROR\nERROR\n");

    app_receive("SERV:090;", 5);
    assert(servo == 90);
    app_tick(404);
    expect("");
    app_tick(405);
    expect("STATE:90,0,0,0\nOK\n");

    app_receive("STEP:FWD,200;", 500);
    assert(forward);
    for (uint32_t now = 500; now < 1320; ++now) {
        app_tick(now);
    }
    assert(pulses == 200);
    expect("STATE:90,200,0,0\nOK\n");
    app_receive("STEP:FWD,1;", 1330);
    expect("ERROR\n");
    app_receive("STEP:REV,200;", 1400);
    assert(!forward);
    for (uint32_t now = 1400; now < 2220; ++now) {
        app_tick(now);
    }
    assert(pulses == 400);
    expect("STATE:90,0,0,0\nOK\n");

    app_receive("PUMP:003;", 2300);
    assert(pump);
    expect("STATE:90,0,1,1\n");
    app_receive("SERV:000;", 2301);
    expect("ERROR\n");
    app_tick(5299);
    assert(pump);
    app_tick(5300);
    assert(!pump);
    expect("STATE:90,0,0,0\nOK\n");
}
#endif

int main(void)
{
#ifdef CONTROLLER
    check_thresholds();
    check_irrigation();
    check_stale_telemetry();
    check_angle_diagnostic();
#else
    check_actuator_commands();
#endif
    puts("Firmware core checks passed.");
}
