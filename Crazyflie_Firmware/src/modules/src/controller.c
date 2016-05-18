/*
The controller should calculate the control signal from the reference
and the current state.
IN: current state, reference
OUT: motor power
*/
#include <math.h>

#include "FreeRTOS.h"
#include "task.h"
#include "ledseq.h"
#include "queue.h"

#include "config.h"
#include "system.h"
#include "controller.h"
#include "debug.h"
#include "motors.h"
#include "sensfusion6.h"
#include "imu.h"
#include "num.h"
#include "attitude_controller.h"
#include "position_estimator.h"
#include "log.h"

#include "param.h"

#include "pm.h"
#include "commander.h"

#define Nstates 8
#define Ninputs 4


#define ATTITUDE_UPDATE_RATE_DIVIDER  2
#define ATTITUDE_UPDATE_DT  (float)(1.0 / (IMU_UPDATE_FREQ / ATTITUDE_UPDATE_RATE_DIVIDER)) // 250hz

static float K[Ninputs][2*Nstates] =
{
  {0.0000003571,0.0000003693,0.0000030538,-0.0000000007,-0.0000000007,-0.0000000061,0.3424464060,0.4139014653,-0.0000000000,0.0000000000,0.0000000000,0.0000003570,0.0000003690,0.0000030538,-0.0000162029,-0.0974366323},
  {0.0045848282,0.0000000034,0.0000000051,0.0034966187,-0.0000000000,-0.0000000000,0.0000000001,-0.0000000301,-0.0010999338,-0.0000000000,-0.0000000000,-0.0000000020,0.0000000034,0.0000000051,-0.0000000301,-0.0000000000},
  {-0.0000000502,0.0047195244,-0.0000000589,0.0000000001,0.0035993842,0.0000000001,-0.0000000007,0.0000003555,-0.0000000000,-0.0011322608,0.0000000000,-0.0000000502,-0.0000000536,-0.0000000589,0.0000003555,0.0000000000},
  {0.0000002454,0.0000001064,0.0071397847,-0.0000000005,-0.0000000002,0.0054451720,0.0000000003,-0.0000001475,-0.0000000000,-0.0000000000,-0.0017128890,0.0000002454,0.0000001064,-0.0000000155,-0.0000001475,-0.0000000000},
};

static float b=0.001;
static float k=0.1;
//static float b=1; // insert values here...
//static float k=1; // insert values here...
static float d=0.05; // insert values here...
static float baseThrust=0;
estimate_t pos;
float speedZ;
static float eulerRollActual_s;   // Measured roll angle in deg
static float eulerPitchActual_s;  // Measured pitch angle in deg
static float eulerYawActual_s;    // Measured yaw angle in deg

static bool isInit;
extern QueueHandle_t xQueue1;
int* REF;
static uint16_t reset_I=1;

uint32_t motorPowerM1;  // Motor 1 power output (16bit value used: 0 - 65535)
uint32_t motorPowerM2;  // Motor 2 power output (16bit value used: 0 - 65535)
uint32_t motorPowerM3;  // Motor 3 power output (16bit value used: 0 - 65535)
uint32_t motorPowerM4;  // Motor 4 power output (16bit value used: 0 - 65535)
static Axis3f gyro; // Gyro axis data in deg/s
static Axis3f acc;  // Accelerometer axis data in mG
static Axis3f mag;  // Magnetometer axis data in testla

float u[Ninputs];
float x[Nstates] = {0,0,0,0,0,0,0,0};
float ref[Nstates] = {0,0,0,0,0,0,0,0};
float xi[Nstates] = {0,0,0,0,0,0,0,0};
static float u_k[Ninputs];
static float thrusts[Ninputs];

static uint16_t limitThrust(int32_t value)
{
  return limitUint16(value);
}

static void ctrlCalc(float states[2*Nstates])
// Calculates control signal given states and state references
// must return a pointer
{
  int i;
  for(i=0;i<Ninputs;i++)
  {
    int j;
    u_k[i]=0;
    for(j=0;j<2*Nstates;j++)
    {
      //u_k[i]=u_k[i]+K[i][j]*(ref[j]-states[j]);
      u_k[i]=u_k[i] - K[i][j]*(states[j]);
    }
  }
}

static void Torque2Thrust(float inputs[Ninputs])
// must return a pointer
{
  //predefine b,d,k
thrusts[0] = -1/(4*b)*inputs[0] -1.4142/(4*b*d)*inputs[1] +1.4142/(4*b*d)*inputs[2] +1/(4*k)*inputs[3];
thrusts[1] = -1/(4*b)*inputs[0] -1.4142/(4*b*d)*inputs[1] -1.4142/(4*b*d)*inputs[2] -1/(4*k)*inputs[3];
thrusts[2] = -1/(4*b)*inputs[0] +1.4142/(4*b*d)*inputs[1] -1.4142/(4*b*d)*inputs[2] +1/(4*k)*inputs[3];
thrusts[3] = -1/(4*b)*inputs[0] +1.4142/(4*b*d)*inputs[1] +1.4142/(4*b*d)*inputs[2] -1/(4*k)*inputs[3];

}

static void controllerTask(void* param)
{
  uint32_t lastWakeTime;
  uint32_t attitudeCounter = 0;

//  vTaskSetApplicationTaskTag(0, (void*)TASK_STABILIZER_ID_NBR); // What is this?

  //Wait for the system to be fully started
  systemWaitStart();

  lastWakeTime = xTaskGetTickCount ();

  while(1)
  {
    vTaskDelayUntil(&lastWakeTime, F2T(IMU_UPDATE_FREQ)); // 500Hz

    imu9Read(&gyro, &acc, &mag);

    //xQueueReceive( xQueue1, &(ref),( TickType_t ) 1 );

    if( imu6IsCalibrated() )
    { // if/else needed?
      // Get ref and sensor
      // ref comes in a queue from ref_generator
      // todo: make REF into a vector
      if (++attitudeCounter >= ATTITUDE_UPDATE_RATE_DIVIDER)
      {

      sensfusion6UpdateQ(gyro.x, gyro.y, gyro.z, acc.x, acc.y, acc.z, ATTITUDE_UPDATE_DT);
      sensfusion6GetEulerRPY(&eulerRollActual_s, &eulerPitchActual_s, &eulerYawActual_s);
      positionUpdateVelocity(sensfusion6GetAccZWithoutGravity(acc.x,acc.y,acc.z), 1/IMU_UPDATE_FREQ);
      positionEstimate(&pos, (float)(0), 1/IMU_UPDATE_FREQ);
      velocityEstimateZ(&x[6]); //x6 = dotZ?
      x[7]=pos.position.z;

      x[0]=eulerRollActual_s;
      x[1]=eulerPitchActual_s;
      x[2]=eulerYawActual_s;
      x[3]=gyro.x;
      x[4]=-gyro.y;
      x[5]=-gyro.z;

      // Integrator states
      float limPos = 1000;
      float limNeg = -1000;
      int i;
      for(i=0;i<Nstates;i++)
      {
        xi[i]+=ref[i]-x[i];
        if(reset_I)
          xi[i]=0;

        if(xi[i]>limPos)
        {
          xi[i]=limPos;
        }else if(xi[i]<limNeg)
        {
          xi[i]=limNeg;
        }
      }

      // Calculate input (T,tx,ty,tz)
      float xxi[2*Nstates] = {x[0],x[1],x[2],x[3],x[4],x[5],x[6],x[7],xi[0],xi[1],xi[2],xi[3],xi[4],xi[5],xi[6],xi[7]};
      ctrlCalc(xxi); // Do not redefine...

      // Translate from (T,tx,ty,tz) to motorPowerMi
      Torque2Thrust(u_k);

      motorPowerM1 = limitThrust(baseThrust + thrusts[0]);
      motorPowerM2 = limitThrust(baseThrust + thrusts[1]);
      motorPowerM3 = limitThrust(baseThrust + thrusts[2]);
      motorPowerM4 = limitThrust(baseThrust + thrusts[3]);

      motorsSetRatio(MOTOR_M1, motorPowerM1);
      motorsSetRatio(MOTOR_M2, motorPowerM2);
      motorsSetRatio(MOTOR_M3, motorPowerM3);
      motorsSetRatio(MOTOR_M4, motorPowerM4);

      // DEBUG
    //  DEBUG_PRINT("Controller debug");
    attitudeCounter = 0;
  }
    }
  }
}


void controllerInit(void)
{
  if(isInit)
    return;

  // Call dependency inits
  motorsInit(motorMapDefaultBrushed);
  imu6Init();
  sensfusion6Init();
  attitudeControllerInit();

  // Create task
  xTaskCreate(controllerTask, CONTROLLER_TASK_NAME,
              CONTROLLER_TASK_STACKSIZE, NULL, CONTROLLER_TASK_PRI, NULL);

  isInit = true;
}

bool controllerTest(void)
{
  bool pass = true;

  pass &= motorsTest();
  pass &= imu6Test();
  pass &= sensfusion6Test();
  pass &= attitudeControllerTest();
  return pass;
}

LOG_GROUP_START(ctrl_rpy)
LOG_ADD(LOG_FLOAT, roll, &eulerRollActual_s)
LOG_ADD(LOG_FLOAT, pitch, &eulerPitchActual_s)
LOG_ADD(LOG_FLOAT, yaw, &eulerYawActual_s)
LOG_GROUP_STOP(ctrl_rpy)

LOG_GROUP_START(acc)
LOG_ADD(LOG_FLOAT, x, &acc.x)
LOG_ADD(LOG_FLOAT, y, &acc.y)
LOG_ADD(LOG_FLOAT, z, &acc.z)
LOG_GROUP_STOP(acc)

LOG_GROUP_START(gyro)
LOG_ADD(LOG_FLOAT, x, &gyro.x)
LOG_ADD(LOG_FLOAT, y, &gyro.y)
LOG_ADD(LOG_FLOAT, z, &gyro.z)
LOG_GROUP_STOP(gyro)

LOG_GROUP_START(mag)
LOG_ADD(LOG_FLOAT, x, &mag.x)
LOG_ADD(LOG_FLOAT, y, &mag.y)
LOG_ADD(LOG_FLOAT, z, &mag.z)
LOG_GROUP_STOP(mag)

LOG_GROUP_START(motor)
LOG_ADD(LOG_INT32, m4, &motorPowerM4)
LOG_ADD(LOG_INT32, m1, &motorPowerM1)
LOG_ADD(LOG_INT32, m2, &motorPowerM2)
LOG_ADD(LOG_INT32, m3, &motorPowerM3)
LOG_GROUP_STOP(motor)

LOG_GROUP_START(thrusts_s)
LOG_ADD(LOG_FLOAT, t1, &thrusts[0])
LOG_ADD(LOG_FLOAT, t2, &thrusts[1])
LOG_ADD(LOG_FLOAT, t3, &thrusts[2])
LOG_ADD(LOG_FLOAT, t4, &thrusts[3])
LOG_ADD(LOG_FLOAT, u1, &u_k[0])
LOG_ADD(LOG_FLOAT, u2, &u_k[1])
LOG_ADD(LOG_FLOAT, u3, &u_k[2])
LOG_ADD(LOG_FLOAT, u4, &u_k[3])
LOG_GROUP_STOP(thrusts_s)

PARAM_GROUP_START(controllerr)
PARAM_ADD(PARAM_FLOAT, b, &b)
PARAM_ADD(PARAM_FLOAT, k, &k)
PARAM_ADD(PARAM_FLOAT, base_thrust, &baseThrust)
PARAM_ADD(PARAM_INT16, resInt, &reset_I)
PARAM_GROUP_STOP(controllerr)
