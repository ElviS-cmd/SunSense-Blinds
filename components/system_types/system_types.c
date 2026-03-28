/**
 * @file system_types.c
 * @brief Shared type definitions and documentation for SunSense system
 * 
 * This file documents the enums and types used across all components.
 */

#include "system_types.h"

/**
 * OperatingMode Enum
 * 
 * Defines the three operating modes of the SunSense system:
 * 
 * MODE_MENU (0)
 *   - Initial state after power-up
 *   - Displays menu on OLED
 *   - User selects AUTO or MANUAL mode
 *   - Press UP/DOWN to navigate, ENTER to select
 *   - Motor is stopped in this mode
 * 
 * MODE_AUTO (1)
 *   - Automatic light-responsive mode
 *   - LDR sensor controls motor behavior:
 *     * Light is bright → Motor OPENS blinds
 *     * Light is dark → Motor CLOSES blinds
 *   - Hysteresis prevents oscillation
 *   - Press and hold ENTER for 2s to return to MENU
 * 
 * MODE_MANUAL (2)
 *   - Manual button control mode
 *   - UP button → Motor OPENS blinds
 *   - DOWN button → Motor CLOSES blinds
 *   - ENTER button → Motor STOPS (or return to MENU with hold)
 *   - No automatic sensor control
 */

/**
 * MotorState Enum
 * 
 * Defines the three possible motor states:
 * 
 * MOTOR_STOPPED (0)
 *   - Motor is not running
 *   - GPIO outputs are LOW-LOW (no voltage on L298N inputs)
 *   - Used in MENU mode and during transitions
 * 
 * MOTOR_OPENING (1)
 *   - Motor running to open blinds (raise them up)
 *   - L298N configuration: IN1=HIGH, IN2=LOW (forward direction)
 *   - Motor continues until:
 *     * User presses ENTER (MANUAL mode)
 *     * LDR transitions to dark (AUTO mode)
 *     * End-of-travel switch triggers (future feature)
 * 
 * MOTOR_CLOSING (2)
 *   - Motor running to close blinds (lower them down)
 *   - L298N configuration: IN1=LOW, IN2=HIGH (reverse direction)
 *   - Motor continues until:
 *     * User presses ENTER (MANUAL mode)
 *     * LDR transitions to bright (AUTO mode)
 *     * End-of-travel switch triggers (future feature)
 */

/**
 * ButtonAction Enum
 * 
 * Defines possible button actions detected by the button controller:
 * 
 * BUTTON_ACTION_NONE (0)
 *   - No button action detected
 *   - Default state between presses
 *   - Actions are cleared after processing each frame
 * 
 * BUTTON_ACTION_UP (1)
 *   - UP button (GPIO13) pressed and released
 *   - Press duration: < 2000ms (not a hold)
 *   - Debounce: 50ms minimum between presses
 *   - Used for: Menu navigation (up), Manual motor open
 * 
 * BUTTON_ACTION_DOWN (2)
 *   - DOWN button (GPIO12) pressed and released
 *   - Press duration: < 2000ms (not a hold)
 *   - Debounce: 50ms minimum between presses
 *   - Used for: Menu navigation (down), Manual motor close
 * 
 * BUTTON_ACTION_ENTER (3)
 *   - ENTER button (GPIO32) pressed and released
 *   - Press duration: < 2000ms (not a hold)
 *   - Debounce: 50ms minimum between presses
 *   - Used for: Menu selection, mode transitions
 *   - Hold >2000ms: Returns from AUTO/MANUAL to MENU
 * 
 * Note: Hold detection (≥2000ms) is handled separately in button logic
 */

/* ==================== System State Relationships ==================== */

/**
 * State Transition Table
 * 
 * Mode          Input              Action                Result
 * ─────────────────────────────────────────────────────────────────
 * MENU          UP                 Move selector up       MENU (index--)
 * MENU          DOWN               Move selector down     MENU (index++)
 * MENU          ENTER              Select mode            AUTO or MANUAL
 * 
 * AUTO          LDR bright         Open blinds            OPENING
 * AUTO          LDR dark           Close blinds           CLOSING
 * AUTO          ENTER (hold 2s)    Return to menu         MENU
 * 
 * MANUAL        UP                 Open blinds            OPENING
 * MANUAL        DOWN               Close blinds           CLOSING
 * MANUAL        ENTER (hold 2s)    Return to menu         MENU
 */

/* ==================== Data Flow Examples ==================== */

/**
 * Example 1: Auto Mode Transition
 * ────────────────────────────────────
 * 
 * 1. System starts in MENU mode
 *    mode_ctrl.current_mode = MODE_MENU
 * 
 * 2. User navigates to "AUTO MODE" and presses ENTER
 *    mode_ctrl.current_mode = MODE_AUTO
 * 
 * 3. LDR sensor reads bright light (light_level = 95%)
 *    ldr_is_bright() → true
 * 
 * 4. Motor task executes:
 *    desired_state = MOTOR_OPENING
 *    motor_ctrl.desired_state = MOTOR_OPENING
 * 
 * 5. GPIO control applied:
 *    GPIO_MOTOR_IN1 = HIGH (forward)
 *    GPIO_MOTOR_IN2 = LOW
 * 
 * 6. Motor opens blinds; light decreases to 45%
 *    ldr_is_bright() → false
 * 
 * 7. Motor task executes:
 *    desired_state = MOTOR_CLOSING
 *    motor_ctrl.desired_state = MOTOR_CLOSING
 * 
 * 8. GPIO control applied:
 *    GPIO_MOTOR_IN1 = LOW
 *    GPIO_MOTOR_IN2 = HIGH (reverse)
 * 
 * (Hysteresis prevents rapid oscillation)
 */

/**
 * Example 2: Manual Mode Transition
 * ─────────────────────────────────
 * 
 * 1. User in AUTO mode presses and holds ENTER (≥2000ms)
 *    button_ctrl.btn_enter.last_action = BUTTON_ACTION_ENTER (hold detected)
 *    mode_ctrl.current_mode = MODE_MENU
 * 
 * 2. Motor task detects MODE_MENU
 *    desired_state = MOTOR_STOPPED
 * 
 * 3. Display task shows menu
 * 
 * 4. User navigates to "MANUAL MODE" and presses ENTER
 *    mode_ctrl.current_mode = MODE_MANUAL
 * 
 * 5. User presses UP button
 *    button_ctrl.btn_up.last_action = BUTTON_ACTION_UP
 * 
 * 6. Motor task reads UP action
 *    desired_state = MOTOR_OPENING
 * 
 * 7. GPIO control: IN1=HIGH, IN2=LOW
 * 
 * 8. User presses DOWN button
 *    button_ctrl.btn_down.last_action = BUTTON_ACTION_DOWN
 * 
 * 9. Motor task reads DOWN action
 *    desired_state = MOTOR_CLOSING
 * 
 * 10. GPIO control: IN1=LOW, IN2=HIGH
 */

/* ==================== Timing Guarantees ==================== */

/**
 * Button Press to Motor Response
 * ───────────────────────────────
 * 
 * Event: User presses UP button
 * │
 * ├─ T+0ms   Button physically pressed
 * │
 * ├─ T+50ms  Button task samples GPIO, debounces
 * │          button_ctrl.btn_up.last_action = BUTTON_ACTION_UP
 * │
 * ├─ T+100ms Mode task reads action (if applicable)
 * │          motor task reads action
 * │          Desired state updated
 * │
 * ├─ T+100ms Motor task applies GPIO changes
 * │          GPIO_MOTOR_IN1 = 1, GPIO_MOTOR_IN2 = 0
 * │
 * └─ T+120ms Motor physically starts spinning
 * 
 * Total latency: 50-120ms
 */

/**
 * Sensor Change to Display Update
 * ────────────────────────────────
 * 
 * Event: Light level crosses hysteresis threshold
 * │
 * ├─ T+0ms    Physical light change
 * │
 * ├─ T+1000ms LDR task samples ADC
 * │           Hysteresis triggers: is_bright transitions
 * │
 * ├─ T+1000ms Motor task reads new LDR state
 * │           Motor state updated
 * │
 * ├─ T+1500ms Display task reads motor state
 * │           Refreshes OLED
 * │
 * └─ T+1500ms User sees updated display
 * 
 * Sensor to display latency: 1000-1500ms
 */