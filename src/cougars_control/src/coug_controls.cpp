#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "pid.cpp"
#include "frost_interfaces/msg/desired_depth.hpp"
#include "frost_interfaces/msg/desired_heading.hpp"
#include "frost_interfaces/msg/desired_speed.hpp"
#include "frost_interfaces/msg/u_command.hpp"
#include "seatrac_interfaces/msg/modem_status.hpp"
#include "geometry_msgs/msg/pose_with_covariance_stamped.hpp"
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/empty.hpp"

using namespace std::chrono_literals;
using std::placeholders::_1;

rmw_qos_profile_t qos_profile = rmw_qos_profile_sensor_data;
auto qos = rclcpp::QoS(
    rclcpp::QoSInitialization(qos_profile.history, qos_profile.depth),
    qos_profile);

/**
 * @brief A simple controls node.
 * @author Nelson Durrant
 * @date September 2024
 *
 * This node subscribes to desired depth, heading, and speed topics and actual
 * depth and heading topics. It then computes the control commands using various
 * controllers and publishes the control commands to a control command topic.
 *
 * Subscribes:
 * - init (std_msgs/msg/Empty)
 * - desired_depth (frost_interfaces/msg/DesiredDepth)
 * - desired_heading (frost_interfaces/msg/DesiredHeading)
 * - desired_speed (frost_interfaces/msg/DesiredSpeed)
 * - depth_data (geometry_msgs/msg/PoseWithCovarianceStamped)
 * - modem_status (seatrac_interfaces/msg/ModemStatus)
 * Publishes:
 * - controls/command (frost_interfaces/msg/UCommand)
 */
class CougControls : public rclcpp::Node {
public:
  /**
   * @brief Creates a new controls node.
   *
   * This constructor creates a new controls node with default values.
   */
  CougControls() : Node("coug_controls") {

    /**
     * @param timer_period
     *
     * The period of the control loop in milliseconds. The default value is
     * 80 ms from experimentation with the BlueRobotics depth sensor.
     */
    this->declare_parameter("timer_period", 80);

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
     * The derivative constant for the depth PID controller. The default value
     * is 0.0.
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
     * The minimum output value for the heading PID controller. The default
     * value is 0.
     */
    this->declare_parameter("heading_min_output", 0);

    /**
     * @param heading_max_output
     *
     * The maximum output value for the heading PID controller. The default
     * value is 0.
     */
    this->declare_parameter("heading_max_output", 0);

    /**
     * @param heading_bias
     *
     * The bias value for the heading PID controller. The default value is 0.
     */
    this->declare_parameter("heading_bias", 0);


    /**
     * @param magnetic_declination
     * 
     * determines the offset to apply to the imu output based on location in degrees. 
     * The default is 10.7 degrees for Utah Lake
     */
    this->declare_parameter("magnetic_declination", 10.7);
    this->magnetic_declination = this->get_parameter("magnetic_declination").as_double();

    // calibrate PID controllers
    myDepthPID.calibrate(this->get_parameter("depth_kp").as_double(),
                         this->get_parameter("depth_ki").as_double(),
                         this->get_parameter("depth_kd").as_double(),
                         this->get_parameter("depth_min_output").as_int(),
                         this->get_parameter("depth_max_output").as_int(),
                         this->get_parameter("timer_period").as_int(),
                         this->get_parameter("depth_bias").as_int());

    myHeadingPID.calibrate(this->get_parameter("heading_kp").as_double(),
                           this->get_parameter("heading_ki").as_double(),
                           this->get_parameter("heading_kd").as_double(),
                           this->get_parameter("heading_min_output").as_int(),
                           this->get_parameter("heading_max_output").as_int(),
                           this->get_parameter("timer_period").as_int(),
                           this->get_parameter("heading_bias").as_int());

    /**
     * @brief Control command publisher.
     *
     * This publisher publishes the control commands to the "control_command"
     * topic. It uses the UCommand message type.
     */
    u_command_publisher_ =
        this->create_publisher<frost_interfaces::msg::UCommand>(
            "control_command", 10);

    /**
     * @brief Initialization subscriber.
     *
     * This subscriber subscribes to the "init" topic. It uses the Empty message
     * type.
     */
    init_subscription_ = this->create_subscription<std_msgs::msg::Empty>(
        "init", 10, std::bind(&CougControls::init_callback, this, _1));

    /**
     * @brief Desired depth subscriber.
     *
     * This subscriber subscribes to the "desired_depth" topic. It uses the
     * DesiredDepth message type.
     */
    desired_depth_subscription_ =
        this->create_subscription<frost_interfaces::msg::DesiredDepth>(
            "desired_depth", 10,
            std::bind(&CougControls::desired_depth_callback, this, _1));

    /**
     * @brief Desired heading subscriber.
     *
     * This subscriber subscribes to the "desired_heading" topic. It uses the
     * DesiredHeading message type.
     */
    desired_heading_subscription_ =
        this->create_subscription<frost_interfaces::msg::DesiredHeading>(
            "desired_heading", 10,
            std::bind(&CougControls::desired_heading_callback, this, _1));

    /**
     * @brief Desired speed subscriber.
     *
     * This subscriber subscribes to the "desired_speed" topic. It uses the
     * DesiredSpeed message type.
     */
    desired_speed_subscription_ =
        this->create_subscription<frost_interfaces::msg::DesiredSpeed>(
            "desired_speed", 10,
            std::bind(&CougControls::desired_speed_callback, this, _1));

    /**
     * @brief Depth subscriber.
     *
     * This subscriber subscribes to the "depth_data" topic. It uses the
     * PoseWithCovarianceStamped message type.
     */
    actual_depth_subscription_ = this->create_subscription<
        geometry_msgs::msg::PoseWithCovarianceStamped>(
        "depth_data", 10,
        std::bind(&CougControls::actual_depth_callback, this, _1));

    /**
     * @brief Yaw subscriber.
     *
     * This subscriber subscribes to the "modem_status" topic. It uses the ModemStatus
     * message type.
     */
    actual_heading_subscription_ =
        this->create_subscription<seatrac_interfaces::msg::ModemStatus>(
            "modem_status", 10,
            std::bind(&CougControls::actual_heading_callback, this, _1));

    /**
     * @brief Control timer.
     *
     * This timer calls the control loop at the specified period.
     */
    controls_timer_ = this->create_wall_timer(
        std::chrono::milliseconds(this->get_parameter("timer_period").as_int()),
        std::bind(&CougControls::timer_callback, this));
  }

private:
  /**
   * @brief Callback function for the init subscription.
   *
   * This method initializes the controls node by setting the init flag to
   * true.
   *
   * @param msg The Empty message recieved from the init topic.
   */
  void init_callback(const std_msgs::msg::Empty::SharedPtr msg) {

    (void)msg; // supress unused variable warning

    RCLCPP_INFO(this->get_logger(), "[INFO] Init message recieved");
    this->init_flag = true;
  }

  /**
   * @brief Callback function for the desired_depth subscription.
   *
   * This method sets the desired depth value to the value received from the
   * desired depth message.
   *
   * @param depth_msg The DesiredDepth message recieved from the desired_depth
   * topic.
   */
  void
  desired_depth_callback(const frost_interfaces::msg::DesiredDepth &depth_msg) {
    this->desired_depth = depth_msg.desired_depth;
  }

  /**
   * @brief Callback function for the desired_heading subscription.
   *
   * This method sets the desired heading value to the value received from the
   * desired heading message.
   *
   * @param heading_msg The DesiredHeading message recieved from the
   * desired_heading topic.
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
   * @param speed_msg The DesiredSpeed message recieved from the desired_speed
   * topic.
   */
  void
  desired_speed_callback(const frost_interfaces::msg::DesiredSpeed &speed_msg) {
    this->desired_speed = speed_msg.desired_speed;
  }

  /**
   * @brief Callback function for the depth subscription.
   *
   * This method sets the actual depth value to the value received from the
   * depth message.
   *
   * @param depth_msg The PoseWithCovarianceStamped message recieved from the
   * depth_data topic.
   */
  void actual_depth_callback(
      const geometry_msgs::msg::PoseWithCovarianceStamped &depth_msg) {
    this->actual_depth = depth_msg.pose.pose.position.z;
  }

  /**
   * @brief Callback function for the yaw subscription.
   *
   * This method sets the actual heading value to the value received from the
   * yaw message.
   *
   * @param heading_msg The ModemStatus message recieved from the modem_status topic.
   */
  void
  actual_heading_callback(const seatrac_interfaces::msg::ModemStatus &heading_msg) {
      //Heading is in degrees east of true north between -180 and 180
      //TODO: make sure this is what we want 
      // (Note: MOOS defines yaw to be negative heading)
      this->actual_heading = 0.1*heading_msg.attitude_yaw + this->magnetic_declination;
      RCLCPP_INFO(this->get_logger(), "[INFO] Yaw Info Recieved: %f",
                  this->actual_heading);
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

      int depth_pos =
          myDepthPID.compute(this->desired_depth, -this->actual_depth);
      int heading_pos =
          myHeadingPID.compute(this->desired_heading, this->actual_heading);

      message.fin[0] = heading_pos;    // top fin
      message.fin[1] = -1 * depth_pos; // right fin (from the front)
      message.fin[2] = depth_pos;      // left fin (from the front)
      message.thruster = this->desired_speed;

      u_command_publisher_->publish(message);
    }
  }

  // micro-ROS objects
  rclcpp::TimerBase::SharedPtr controls_timer_;
  rclcpp::Publisher<frost_interfaces::msg::UCommand>::SharedPtr
      u_command_publisher_;
  rclcpp::Subscription<frost_interfaces::msg::DesiredDepth>::SharedPtr
      desired_depth_subscription_;
  rclcpp::Subscription<frost_interfaces::msg::DesiredHeading>::SharedPtr
      desired_heading_subscription_;
  rclcpp::Subscription<frost_interfaces::msg::DesiredSpeed>::SharedPtr
      desired_speed_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::PoseWithCovarianceStamped>::SharedPtr
      actual_depth_subscription_;
  rclcpp::Subscription<seatrac_interfaces::msg::ModemStatus>::SharedPtr
      actual_heading_subscription_;
  rclcpp::Subscription<std_msgs::msg::Empty>::SharedPtr init_subscription_;

  // node initialization flag
  bool init_flag = false;

  // control objects
  PID myHeadingPID;
  PID myDepthPID;

  // magnetic declination parameter
  double magnetic_declination;

  // node desired values
  float desired_depth = 0.0;
  float desired_heading = 0.0;
  float desired_speed = 0.0;

  // node actual values
  float actual_depth = 0.0;
  float actual_heading = 0.0;
};

int main(int argc, char *argv[]) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<CougControls>());
  rclcpp::shutdown();
  return 0;
}
