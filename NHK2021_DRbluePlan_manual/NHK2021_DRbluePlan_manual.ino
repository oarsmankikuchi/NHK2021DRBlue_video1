//NHK学生ロボコン2021 DR青プランのマニュアル制御
//最終更新　2021/02/24

#include <Arduino.h>
#include <MsTimer2.h>

#include "define.h"
#include "ManualControl.h"
#include "Controller.h"
#include "phaseCounterPeach.h"
#include "lpms_me1Peach.h"
#include "RoboClaw.h"
#include "PIDclass.h"
#include "DRBlue.h"
#include "Button_Encorder.h"
#include "LCDclass.h"

lpms_me1 lpms(&SERIAL_LPMSME1);
phaseCounter encX(1);
phaseCounter encY(2);
DRBlue DR(&lpms, &encX, &encY); //DRのセットアップなどを行う

PID posiZ_pid(0.0,0.0,0.0,INT_TIME);
ManualControl ManualCon(&posiZ_pid); //メカナムの速度制御
Controller Con(&SERIAL_XBEE); //dualshock4
myLCDclass lcd(&SERIAL_LCD);

RoboClaw roboclaw(&SERIAL_ROBOCLAW,1000);
DRwall wall_1(PIN_SW_WALL_1,PIN_SUPPORT_WHEEL_1,ADR_MD_WHEE_1,&roboclaw); // 壁越えの制御を行う
DRwall wall_2(PIN_SW_WALL_2,PIN_SUPPORT_WHEEL_2,ADR_MD_WHEE_2,&roboclaw); //　↓
DRwall wall_3(PIN_SW_WALL_3,PIN_SUPPORT_WHEEL_3,ADR_MD_WHEE_3,&roboclaw); //　↓
DRwall wall_4(PIN_SW_WALL_4,PIN_SUPPORT_WHEEL_4,ADR_MD_WHEE_4,&roboclaw); //　↓

DRexpand expand_right(PIN_SW_EXPAND_RIGHT,PIN_SUPPORT_RIGHT); // 展開機構
DRexpand expand_left(PIN_SW_EXPAMD_LEFT,PIN_SUPPORT_LEFT);    // ↓

Encorder enc; // 基板上のエンコーダ
int encorder_count; //エンコーダのカウント値を格納
DipSW dipsw; // 基板上のディップスイッチ
int dipsw_state; //ディップスイッチの状態を格納

/****基板上のスイッチ****/
Button button_red(PIN_SW_RED);
bool button_RED = false;
Button button_black(PIN_SW_BLACK);
bool button_BLACK = false;
Button button_up(PIN_SW_UP);
bool button_UP = false;
Button button_down(PIN_SW_DOWN);
bool button_DOWN = false;
Button button_right(PIN_SW_RIGHT);
bool button_RIGHT = false;
Button button_left(PIN_SW_LEFT);
bool button_LEFT = false;

/****変数****/
bool flag_10ms  = false;

int expand_mode; // 展開方法に関する変数
int robot_velocity_mode; //足回りの走行仕様について
int wall_mode; // 壁越え機構に関する変数
int turning_mode; //ロボットの旋回モード

void dipSetup(){  
  if(dipsw_state & DIP1){
    expand_mode = 2; // 2段階目をコントローラから指示する
    digitalWrite(PIN_LED_1,HIGH);
  }  
  else{
    digitalWrite(PIN_LED_1,LOW);
    expand_mode = 1; // 2段階目をリミットスイッチで判断
  } 

  if(dipsw_state & DIP2){
    
    robot_velocity_mode = 1; //X方向にのみ進む
    digitalWrite(PIN_LED_2,HIGH);
  }
  else{
    robot_velocity_mode = 2; //全方向移動
    digitalWrite(PIN_LED_2,LOW);
  }

  if(dipsw_state & DIP3){
    wall_mode = 1; //テストモード
    digitalWrite(PIN_LED_3,HIGH);
  }
  else{
    wall_mode = 2; //通常モード
    digitalWrite(PIN_LED_3,LOW);
  }

  if(dipsw_state & DIP4){
    turning_mode = 1; // 角度PID無し
    digitalWrite(PIN_LED_4,HIGH);
    digitalWrite(PIN_LED_ENC,LOW);
  }
  else{
    turning_mode = 2; // 角度PID有
    digitalWrite(PIN_LED_4,LOW);
    digitalWrite(PIN_LED_ENC,HIGH);
  }
}

void radianPID_setup(){
  static int pid_setting_mode = 1;
  static double Kp = 0.0, Ki = 0.0, Kd = 0.0;
  static bool init_kp = true, init_ki = true, init_kd = true;
  static bool flag_lcd = true;

  if(button_UP)   pid_setting_mode++;
  else if(button_DOWN) pid_setting_mode--;
  if(pid_setting_mode == 0) pid_setting_mode = 3;
  else if(pid_setting_mode == 4) pid_setting_mode = 1;
  
  if(flag_lcd){
    lcd.write_str("RadianPID Setting",LINE_1,1);
    flag_lcd = false;
  }

  switch (pid_setting_mode)
  {
  case 1:
    init_ki = true;
    init_kd = true;
    if(init_kp){
      lcd.write_str("Kp ",LINE_3,1); //3コマ使用
      enc.setEncCount((int)(10.0 * Kp));
      init_kp = false;
    }
    Kp = 0.1*(double)encorder_count;
    lcd.write_str("          ",LINE_3,4);
    lcd.write_double(Kp,LINE_3,4);
    break;
  
  case 2:
    init_kp = true;
    init_kd = true;
    if(init_ki){
      lcd.write_str("Ki ",LINE_3,1); //3コマ使用
      enc.setEncCount((int)(10.0 * Ki));
      init_ki = false;
    }
    Ki = 0.1*(double)encorder_count;
    lcd.write_str("          ",LINE_3,4);
    lcd.write_double(Ki,LINE_3,4);
    break;
  
  case 3:
    init_kp = true;
    init_ki = true;
    if(init_kd){
      lcd.write_str("Kd ",LINE_3,1); //3コマ使用
      enc.setEncCount((int)(10.0 * Kd));
      init_kd = false;
    }
    Kd = 0.1*(double)encorder_count;
    lcd.write_str("          ",LINE_3,4);
    lcd.write_double(Kd,LINE_3,4);
    break;

  default:
    break;
  }
  posiZ_pid.setPara(Kp,Ki,Kd);
}

// 最大最小範囲に収まるようにする関数
double min_max(double value, double minmax){
  if(value > minmax) value = minmax;
  else if(value < -minmax) value = -minmax;
  return value;
}

/****割込みの関数****/
void timer_warikomi(){
  DR.RGB_led(2);
  //DR.calcu_robotPosition();
  DR.calcu_roboAngle(); // calcu_robotPosition関数を使用する場合は不要

  wall_1.wall_time_count(INT_TIME); // 壁越えの時間に関する処理
  wall_2.wall_time_count(INT_TIME); // ↓
  wall_3.wall_time_count(INT_TIME); // ↓
  wall_4.wall_time_count(INT_TIME); // ↓

  encorder_count = enc.getEncCount(); //エンコーダのカウント値を更新
  dipsw_state = dipsw.getDipState(); // ディップスイッチの状態を更新
  button_RED = button_red.button_fall();
  button_BLACK = button_black.button_fall();
  button_UP = button_up.button_fall();
  button_DOWN = button_down.button_fall();
  button_RIGHT = button_right.button_fall();
  button_LEFT = button_left.button_fall();

  flag_10ms = true;
}

void setup(){

  Serial.begin(115200);
  //SERIAL_LEONARDO.begin(9600);
  SERIAL_LCD.begin(115200);

  DR.DRsetup();      //　汎用基板などの様々なセットアップを行う
  DR.BasicSetup();   //　汎用基板などの様々なセットアップを行う
  DR.allOutputLow(); //  出力をLOWにする

  roboclaw.begin(115200);
  SERIAL_XBEE.begin(115200);
  //Con.begin_api(115200); // apiでの通信

  lcd.clear_display();
  lcd.color_red();
  lcd.write_str("waiting....",LINE_2,1);
  
  //ボードのスイッチが押されるまで待機
  bool ready_to_start = false;
  while(!ready_to_start){
    Con.update(PIN_LED_USER);
    //roboclawの原点出し
    if(Con.readButton(BUTTON_PS) == PUSHED){  
      roboclaw.ResetEncoders(ADR_MD_WHEE_1);
      roboclaw.ResetEncoders(ADR_MD_WHEE_2);
      roboclaw.ResetEncoders(ADR_MD_WHEE_3);
      roboclaw.ResetEncoders(ADR_MD_WHEE_4);
    }
    if(Con.getButtonState() & BUTTON_MARU || !digitalRead(PIN_SW)){
      DR.LEDblink(PIN_LED_BLUE, 2, 100);
      lcd.clear_display();
      lcd.color_blue();
      lcd.clear_display();

      lcd.write_str("  HELLOW WORLD   ",LINE_2,1);
      delay(1000);
      ready_to_start = true;
    }
  }

  MsTimer2::set(10,timer_warikomi); // 10ms period
  MsTimer2::start();
}

void loop(){

  Con.update(PIN_LED_USER); //コントローラの状態を更新
  dipSetup(); // ディップスイッチでの設定
  if(turning_mode == 2 ) radianPID_setup(); // pidのゲインを設定
/*
  uint8_t led_num = 0;
  while(SERIAL_LEONARDO.available()){
    char recv =  SERIAL_LEONARDO.read();
    if(recv != '\n'){
      led_num = (uint8_t)recv;
      digitalWrite(PIN_LED_1, led_num & 0x01);
      digitalWrite(PIN_LED_2, led_num & 0x02);
      digitalWrite(PIN_LED_3, led_num & 0x04);
      digitalWrite(PIN_LED_4, led_num & 0x08);
      Serial.println(led_num);
    }
  }
*/

  //展開前の状態に戻す
  if(Con.readButton(BUTTON_PAD) == PUSHED){
    expand_right.init();
    expand_left.init();
  }

  //roboclawの原点出し
  if(Con.readButton(BUTTON_PS) == PUSHED){  
    roboclaw.ResetEncoders(ADR_MD_WHEE_1);
    roboclaw.ResetEncoders(ADR_MD_WHEE_2);
    roboclaw.ResetEncoders(ADR_MD_WHEE_3);
    roboclaw.ResetEncoders(ADR_MD_WHEE_4);
  }
  
  if( flag_10ms ){

    expand_right.expand_func(Con.readButton(BUTTON_R1),expand_mode);
    expand_left.expand_func(Con.readButton(BUTTON_L1),expand_mode);
  
    double Cx,Cy,Cz; //速度の倍数
    Cx = 0.5, Cy = 0.5, Cz = 1.0;
    coords gloabalVel = ManualCon.getGlobalVel(Con.LJoyX,Con.LJoyY,Con.RJoyY);
    coords localVel = ManualCon.getLocalVel(Cx*gloabalVel.x, Cy*gloabalVel.y, Cz*gloabalVel.z, DR.roboAngle);
    if(turning_mode == 1) localVel.z = gloabalVel.z; // 角度PIDをするかしないか
    coords refV = ManualCon.getVel_max(localVel.x, localVel.y, localVel.z);
    coords_4 VelCmd = ManualCon.getCmd(refV.x,refV.y,refV.z);
  /*
    roboclaw.SpeedM1(wall_1.adress,VelCmd.i);   // 足回りの速度指令
    roboclaw.SpeedM1(wall_2.adress,VelCmd.ii);  // ↓
    roboclaw.SpeedM1(wall_3.adress,VelCmd.iii); // ↓
    roboclaw.SpeedM1(wall_4.adress,VelCmd.iv);  // ↓
  */
    wall_1.roboclawSpeedM1(VelCmd.i);   // 足回りの速度指令
    wall_2.roboclawSpeedM1(VelCmd.ii);  // ↓
    wall_3.roboclawSpeedM1(VelCmd.iii); // ↓
    wall_4.roboclawSpeedM1(VelCmd.iv);  // ↓

    double robot_x_vel; //ロボットのx方向の移動速度
    robot_x_vel = 0.5;
    static double wall_rad = 0.0; //壁越え機構の回転角
    static double pre_wall_rad = 0.0;
    static double wall_seconds = 0.0;
    if(Con.readButton(BUTTON_LEFT) == PUSHED) wall_rad = 0.0;
    if(Con.readButton(BUTTON_UP) == PUSHED) wall_rad = 90.0;
    if(Con.readButton(BUTTON_RIGHT) == PUSHED) wall_rad = 180.0;

    if(fabs(pre_wall_rad - wall_rad) == 180.0) wall_seconds = 1.0;
    else wall_seconds = 0.5;
    
    switch (wall_mode)
    {
    case 1:
    /*
      if(wall_1.send_wall_position(wall_rad)){
        roboclaw.SpeedAccelDeccelPositionM2(wall_1.adress,wall_1.accel,wall_1.omega,wall_1.accel,wall_1.position,true);
      }
      if(wall_2.send_wall_position(wall_rad)){
        roboclaw.SpeedAccelDeccelPositionM2(wall_2.adress,wall_2.accel,wall_2.omega,wall_2.accel,wall_2.position,true);
      }
      if(wall_3.send_wall_position(wall_rad)){
        roboclaw.SpeedAccelDeccelPositionM2(wall_3.adress,wall_3.accel,wall_3.omega,wall_3.accel,wall_3.position,true);
      }
      if(wall_4.send_wall_position(wall_rad)){
        roboclaw.SpeedAccelDeccelPositionM2(wall_4.adress,wall_4.accel,wall_4.omega,wall_4.accel,wall_4.position,true);
      }
    */
      wall_1.send_wall_position(wall_rad, wall_seconds);
      wall_2.send_wall_position(wall_rad, wall_seconds);
      wall_3.send_wall_position(wall_rad, wall_seconds);
      wall_4.send_wall_position(wall_rad, wall_seconds);
      break;
    
    case 2:
    /*
      if(wall_1.send_wall_cmd(180.0,robot_x_vel)){
        roboclaw.SpeedAccelDeccelPositionM2(wall_1.adress,wall_1.accel,wall_1.omega,wall_1.accel,wall_1.position,true);
      }
      if(wall_2.send_wall_cmd(180.0,robot_x_vel)){
        roboclaw.SpeedAccelDeccelPositionM2(wall_2.adress,wall_2.accel,wall_2.omega,wall_2.accel,wall_2.position,true);
      }
      if(wall_3.send_wall_cmd(180.0,robot_x_vel)){
        roboclaw.SpeedAccelDeccelPositionM2(wall_3.adress,wall_3.accel,wall_3.omega,wall_3.accel,wall_3.position,true);
      }
      if(wall_4.send_wall_cmd(180.0,robot_x_vel)){
        roboclaw.SpeedAccelDeccelPositionM2(wall_4.adress,wall_4.accel,wall_4.omega,wall_4.accel,wall_4.position,true);
      }
    */
      wall_1.send_wall_cmd(180.0,robot_x_vel);
      wall_2.send_wall_cmd(180.0,robot_x_vel);
      wall_3.send_wall_cmd(180.0,robot_x_vel);
      wall_4.send_wall_cmd(180.0,robot_x_vel);
      break;

    default:
      break;
    }

    flag_10ms = false;
  }
}