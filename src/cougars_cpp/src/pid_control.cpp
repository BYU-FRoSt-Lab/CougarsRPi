#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "cougars_cpp/pid.h"
#include "frost_interfaces/msg/desired_depth.hpp"
#include "frost_interfaces/msg/desired_heading.hpp"
#include "frost_interfaces/msg/desired_speed.hpp"
#include "frost_interfaces/msg/modem_rec.hpp"
#include "frost_interfaces/msg/u_command.hpp"
#include "std_msgs/msg/empty.hpp"
#include "geometry_msgs/msg/pose_with_covariance_stamped.hpp"
#include "rclcpp/rclcpp.hpp"

// TODO: Add to cougars_control package

using namespace std::chrono_literals;
using std::placeholders::_1;

rmw_qos_profile_t qos_profile = rmw_qos_profile_sensor_data;
auto qos = rclcpp::QoS(
    rclcpp::QoSInitialization(qos_profile.history, qos_profile.depth),
    qos_profile);

/**
 * @brief A simple PID control node.
 * @author Nelson Durrant
 * @date September 2024
 * 
 * This node subscribes to desired depth, heading, and speed topics and actual
 * depth and heading topics. It then computes the control commands using PID
 * controllers and publishes the control commands to a control command topic.
 * 
 * Subscribes:
 * - desired_depth (frost_interfaces/msg/DesiredDepth)
 * - desired_heading (frost_interfaces/msg/DesiredHeading)
 * - desired_speed (frost_interfaces/msg/DesiredSpeed)
 * - depth_data (geometry_msgs/msg/PoseWithCovarianceStamped)
 * - modem_rec (frost_interfaces/msg/ModemRec)
 * Publishes:
 * - control_command (frost_interfaces/msg/UCommand)
 */
class PIDControl : public rclcpp::Node {
public:
  /**
   * @brief Creates a new PID control node.
   *
   * This constructor creates a new PID control node with default values.
   */
  PIDControl() : Node("pid_control") {

    /**
     * @param trim_ratio
     * 
     * The trim ratio is used to adjust the control commands to account for
     * thruster trim. The default value is 0.0.
     */
    this->declare_parameter("trim_ratio", 0.0)

    /**
     * @param pid_timer_period
     * 
     * The period of the PID control loop in milliseconds. The default value is
     * 80 ms from experimentation with the BlueRobotics depth sensor.
     */
    this->declare_parameter("pid_timer_period", 80);

    /**
     * @param depth_kp
     * 
     * The proportional constant for the depth PID controller. The default value
     * is 0.0.
     */
    this->declare_parameter("depth_kp", 0.0);

    /**
     * @param depth_ki
     * 
     * The integral constant for the depth PID controller. The default value is
     * 0.0.
     */
    this->declare_parameter("depth_ki", 0.0);

    /**
     * @param depth_kd
     * 
     * The derivative constant for the depth PID controller. The default value is
     * 0.0.
     */
    this->declare_parameter("depth_kd", 0.0);

    /**
     * @param depth_min_output
     * 
     * The minimum output value for the depth PID controller. The default value
     * is 0.
     */
    this->declare_parameter("depth_min_output", 0);

    /**
     * @param depth_max_output
     * 
     * The maximum output value for the depth PID controller. The default value
     * is 0.
     */
    this->declare_parameter("depth_max_output", 0);

    /**
     * @param depth_bias
     * 
     * The bias value for the depth PID controller. The default value is 0.
     */
    this->declare_parameter("depth_bias", 0);

    /**
     * @param heading_kp
     * 
     * The proportional constant for the heading PID controller. The default
     * value is 0.0.
     */
    this->declare_parameter("heading_kp", 0.0);

    /**
     * @param heading_ki
     * 
     * The integral constant for the heading PID controller. The default value
     * is 0.0.
     */
    this->declare_parameter("heading_ki", 0.0);

    /**
     * @param heading_kd
     * 
     * The derivative constant for the heading PID controller. The default value
     * is 0.0.
     */
    this->declare_parameter("heading_kd", 0.0);

    /**
     * @param heading_min_output
     * 
     * The minimum output value for the heading PID controller. The default value
     * is 0.
     */
    this->declare_parameter("heading_min_output", 0);

    /**
     * @param heading_max_output
     * 
     * The maximum output value for the heading PID controller. The default value
     * is 0.
     */
    this->declare_parameter("heading_max_output", 0);

    /**
     * @param heading_bias
     * 
     * The bias value for the heading PID controller. The default value is 0.
     */
    this->declare_parameter("heading_bias", 0);

    /**
     * @param speed_kp
     * 
     * The proportional constant for the speed PID controller. The default value
     * is 0.0.
     */
    this->declare_parameter("speed_kp", 0.0);

    /**
     * @param speed_ki
     * 
     * The integral constant for the speed PID controller. The default value
     * is 0.0.
     */
    this->declare_parameter("speed_ki", 0.0);

    /**
     * @param speed_kd
     * 
     * The derivative constant for the speed PID controller. The default value
     * is 0.0.
     */
    this->declare_parameter("speed_kd", 0.0);

    /**
     * @param speed_min_output
     * 
     * The minimum output value for the speed PID controller. The default value
     * is 0.
     */
    this->declare_parameter("speed_min_output", 0);

    /**
     * @param speed_max_output
     * 
     * The maximum output value for the speed PID controller. The default value
     * is 0.
     */
    this->declare_parameter("speed_max_output", 0);

    /**
     * @param speed_bias
     * 
     * The bias value for the speed PID controller. The default value is 0.
     */
    this->declare_parameter("speed_bias", 0);

    /**
     * @param top_fin_offset
     * 
     * The offset for the fin to make it align properly. The default value is 0.0.
     */
    this->declare_parameter("top_fin_offset", 0.0);

    /**
     * @param right_fin_offset
     * 
     * The offset for the fin to make it align properly. The default value is 0.0.
     */
    this->declare_parameter("right_fin_offset", 0.0);

    /**
     * @param left_fin_offset
     * 
     * The offset for the fin to make it align properly. The default value is 0.0.
     */
    this->declare_parameter("left_fin_offset", 0.0);

    // calibrate PID controllers
    myDepthPID.calibrate(this->get_parameter("depth_kp").as_double(),
                         this->get_parameter("depth_ki").as_double(),
                         this->get_parameter("depth_kd").as_double(),
                         this->get_parameter("depth_min_output").as_int(),
                         this->get_parameter("depth_max_output").as_int(),
                         this->get_parameter("pid_timer_period").as_int(),
                         this->get_parameter("depth_bias").as_int());

    myHeadingPID.calibrate(this->get_parameter("heading_kp").as_double(),
                           this->get_parameter("heading_ki").as_double(),
                           this->get_parameter("heading_kd").as_double(),
                           this->get_parameter("heading_min_output").as_int(),
                           this->get_parameter("heading_max_output").as_int(),
                           this->get_parameter("pid_timer_period").as_int(),
                           this->get_parameter("heading_bias").as_int());

    myVelocityPID.calibrate(this->get_parameter("speed_kp").as_double(),
                            this->get_parameter("speed_ki").as_double(),
                            this->get_parameter("speed_kd").as_double(),
                            this->get_parameter("speed_min_output").as_int(),
                            this->get_parameter("speed_max_output").as_int(),
                            this->get_parameter("pid_timer_period").as_int(),
                            this->get_parameter("speed_bias").as_int());

    /**
     * @brief Creates a new control command publisher.
     * 
     * This publisher publishes the control commands to the "control_command" topic.
     * It uses the UCommand message type.
     */
    u_command_publisher_ =
        this->create_publisher<frost_interfaces::msg::UCommand>(
            "control_command", 10);

    /**
     * @brief Creates a new initialization subscriber.
     * 
     * This subscriber subscribes to the "init" topic. It uses the Empty message
     * type.
     */
    init_subscription_ = 
        this->create_subscription<std_msgs::msg::Empty>(
            "init", 10,
            std::bind(&PIDControl::init_callback, this, _1));

    /**
     * @brief Creates a new desired depth subscriber.
     * 
     * This subscriber subscribes to the "desired_depth" topic. It uses the
     * DesiredDepth message type.
     */
    desired_depth_subscription_ =
        this->create_subscription<frost_interfaces::msg::DesiredDepth>(
            "desired_depth", 10,
            std::bind(&PIDControl::desired_depth_callback, this, _1));

    /**
     * @brief Creates a new desired heading subscriber.
     * 
     * This subscriber subscribes to the "desired_heading" topic. It uses the
     * DesiredHeading message type.
     */
    desired_heading_subscription_ =
        this->create_subscription<frost_interfaces::msg::DesiredHeading>(
            "desired_heading", 10,
            std::bind(&PIDControl::desired_heading_callback, this, _1));

    /**
     * @brief Creates a new desired speed subscriber.
     * 
     * This subscriber subscribes to the "desired_speed" topic. It uses the
     * DesiredSpeed message type.
     */
    desired_speed_subscription_ =
        this->create_subscription<frost_interfaces::msg::DesiredSpeed>(
            "desired_speed", 10,
            std::bind(&PIDControl::desired_speed_callback, this, _1));

    /**
     * @brief Creates a new depth subscriber.
     * 
     * This subscriber subscribes to the "depth_data" topic. It uses the
     * PoseWithCovarianceStamped message type.
     */
    depth_subscription_ = this->create_subscription<
        geometry_msgs::msg::PoseWithCovarianceStamped>(
        "depth_data", 10, std::bind(&PIDControl::depth_callback, this, _1));

    /**
     * @brief Creates a new yaw subscriber.
     * 
     * This subscriber subscribes to the "modem_rec" topic. It uses the ModemRec
     * message type.
     */
    yaw_subscription_ =
        this->create_subscription<frost_interfaces::msg::ModemRec>(
            "modem_rec", 10, std::bind(&PIDControl::yaw_callback, this, _1));

    /**
     * @brief Creates a new PID control timer.
     * 
     * This timer calls the PID control loop at the specified period.
     */
    pid_timer_ = this->create_wall_timer(
        std::chrono::milliseconds(
            this->get_parameter("pid_timer_period").as_int()),
        std::bind(&PIDControl::timer_callback, this));
  }

private:

  /**
   * @brief Callback function for the init subscription.
   * 
   * This method initializes the PID control node by setting the init flag to
   * true.
   * 
   * @param msg The Empty message recieved from the init topic.
   */
  void init_callback(const std_msgs::msg::Empty::SharedPtr msg) {
    this->init_flag = true;
  }

  /**
   * @brief Callback function for the desired_depth subscription.
   * 
   * This method sets the desired depth value to the value received from the
   * desired depth message.
   * 
   * @param depth_msg The DesiredDepth message recieved from the desired_depth topic.
   */
  void desired_depth_callback(const frost_interfaces::msg::DesiredDepth &depth_msg) {
    this->desired_depth = depth_msg.desired_depth;
  }

  /**
   * @brief Callback function for the desired_heading subscription.
   * 
   * This method sets the desired heading value to the value received from the
   * desired heading message.
   * 
   * @param heading_msg The DesiredHeading message recieved from the desired_heading topic.
   */
  void desired_heading_callback(
      const frost_interfaces::msg::DesiredHeading &heading_msg) {
    this->desired_heading = heading_msg.desired_heading;
  }

  /**
   * @brief Callback function for the desired_speed subscription.
   * 
   * This method sets the desired speed value to the value received from the
   * desired speed message.
   * 
   * @param speed_msg The DesiredSpeed message recieved from the desired_speed topic.
   */
  void
  desired_speed_callback(const frost_interfaces::msg::DesiredSpeed &speed_msg) {
    this->desired_speed = speed_msg.desired_speed;
  }

  /**
   * @brief Callback function for the depth subscription.
   * 
   * This method sets the depth value to the value received from the depth
   * message.
   * 
   * @param depth_msg The PoseWithCovarianceStamped message recieved from the depth_data topic.
   */
  void depth_callback(
      const geometry_msgs::msg::PoseWithCovarianceStamped &depth_msg) {
    this->depth = depth_msg.pose.pose.position.z;
  }

  /**
   * @brief Callback function for the yaw subscription.
   * 
   * This method sets the yaw value to the value received from the yaw message.
   * 
   * @param yaw_msg The ModemRec message recieved from the modem_rec topic.
   */
  void yaw_callback(const frost_interfaces::msg::ModemRec &yaw_msg) {

    // Check if the message is a status message
    if (yaw_msg.msg_id == 0x10) {
      this->yaw = yaw_msg.attitude_yaw;
    }
  }

  /**
   * @brief Callback function for the PID control timer.
   * 
   * This method computes the control commands using the PID controllers and
   * publishes the control commands to the control_command topic.
   */
  void timer_callback() {
    auto message = frost_interfaces::msg::UCommand();
    message.header.stamp = this->now();

    if (this->init_flag) {

      int depth_pos = myDepthPID.compute(this->desired_depth, -depth);
      int heading_pos = myHeadingPID.compute(this->desired_heading, yaw);
      int velocity_level =
          this->desired_speed; // myVelocityPID.compute(this->desired_speed,
                              // x_velocity);

      // the "trim_ratio" parameter is used to adjust the control commands to
      // account for thruster trim, depending on the velocity level
      //TOP FIN:
      message.fin[0] = heading_pos + this->get_parameter("trim_ratio").as_double() * velocity_level + this->get_parameter("top_fin_offset").as_double();
      //TODO: RIGHT FIN?
      message.fin[1] = depth_pos + this->get_parameter("trim_ratio").as_double() * velocity_level + this->get_parameter("right_fin_offset").as_double();
      //TODO: LEFT FIN?
      message.fin[2] = depth_pos + this->get_parameter("trim_ratio").as_double() * velocity_level + this->get_parameter("left_fin_offset").as_double();
      message.thruster = velocity_level;

      u_command_publisher_->publish(message);

      RCLCPP_INFO(this->get_logger(),
                  "[INFO] Bottom Servos: %d, Top Servo: %d, Thruster: %d", depth_pos,
                  heading_pos, velocity_level);

    }
  }

  // micro-ROS objects
  rclcpp::TimerBase::SharedPtr pid_timer_;
  rclcpp::Publisher<frost_interfaces::msg::UCommand>::SharedPtr
      u_command_publisher_;
  rclcpp::Subscription<frost_interfaces::msg::DesiredDepth>::SharedPtr
      desired_depth_subscription_;
  rclcpp::Subscription<frost_interfaces::msg::DesiredHeading>::SharedPtr
      desired_heading_subscription_;
  rclcpp::Subscription<frost_interfaces::msg::DesiredSpeed>::SharedPtr
      desired_speed_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::PoseWithCovarianceStamped>::SharedPtr
      depth_subscription_;
  rclcpp::Subscription<frost_interfaces::msg::ModemRec>::SharedPtr
      yaw_subscription_;
  rclcpp::Subscription<std_msgs::msg::Empty>::SharedPtr init_subscription_;

  // node initialization flag
  bool init_flag = false;

  // control objects
  PID myHeadingPID;
  PID myDepthPID;
  PID myVelocityPID;

  // node desired values
  float desired_depth = 0.0;
  float desired_heading = 0.0;
  float desired_speed = 0.0;

  // node actual values
  float yaw = 0.0;
  float x_velocity = 0.0;
  float depth = 0.0;
};

int main(int argc, char *argv[]) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<PIDControl>());
  rclcpp::shutdown();
  return 0;
}
