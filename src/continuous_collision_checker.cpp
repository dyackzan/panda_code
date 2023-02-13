#include <memory>
#include <string>
#include <vector>
#include <deque>
#include <ros/ros.h>
#include <moveit/robot_model_loader/robot_model_loader.h>
#include <moveit/robot_model/robot_model.h>
#include <moveit/robot_model/joint_model.h>
#include <moveit/robot_state/robot_state.h>
#include <moveit/robot_state/conversions.h>
#include <moveit/planning_scene_monitor/planning_scene_monitor.h>
#include <moveit/collision_detection_bullet/collision_env_bullet.h>
#include <moveit/collision_detection_bullet/collision_detector_allocator_bullet.h>
#include <std_msgs/Float64MultiArray.h>
#include <trajectory_msgs/JointTrajectory.h>
#include <robo_demo_msgs/JointTrajectoryPointStamped.h>
#include <robo_demo_msgs/JointTrajectoryPointClearStamped.h>
#include <sensor_msgs/JointState.h>
#include <moveit_msgs/RobotState.h>
#include <ros/serialization.h>

class ContinuousCollisionChecker
{
protected:
  robot_model_loader::RobotModelLoaderPtr rm_loader_ptr_;
  planning_scene_monitor::PlanningSceneMonitorPtr psm_ptr_;
  ros::NodeHandle nh_;
  moveit::core::RobotState state1_, state2_;
  ros::Subscriber current_path_sub_, executing_to_state_sub_, new_goal_sub_;
  ros::Publisher edge_clear_pub_;
  std::deque<std::vector<double>> current_path_ {};
  bool edge_clear_ {false}, new_edge_ {false}, prev_edge_clear_ {false};
  int current_path_cb_count_ {0};
  int executing_to_state_cb_count_ {0};
  int edge_clear_pub_count_ {0};

public:
  ContinuousCollisionChecker(ros::NodeHandle nh) :
    rm_loader_ptr_(std::make_shared<robot_model_loader::RobotModelLoader>("robot_description")),
    psm_ptr_(std::make_shared<planning_scene_monitor::PlanningSceneMonitor>(rm_loader_ptr_)),
    state1_(robot_state::RobotState(rm_loader_ptr_->getModel())),
    state2_(robot_state::RobotState(rm_loader_ptr_->getModel()))
  {
    psm_ptr_->startSceneMonitor();
    psm_ptr_->startWorldGeometryMonitor();
    psm_ptr_->startStateMonitor();
    if (psm_ptr_->requestPlanningSceneState("/get_planning_scene"))
      ROS_INFO("Successfully retrieved planning scene state");
    else
      ROS_WARN("Unseccessful request to get planning scene state");
    nh_ = nh;
    initSubsPubs();
  }

  void initSubsPubs()
  {
    // Planner publishes when a new solution path is found
    current_path_sub_ = nh_.subscribe("/current_path", 1, &ContinuousCollisionChecker::currentPathCb, this);
    // Controller publishes progress
    executing_to_state_sub_ = nh_.subscribe("/executing_to_state", 1, &ContinuousCollisionChecker::executingToStateCb,
                                            this);
    new_goal_sub_ = nh_.subscribe("/new_planner_goal", 1, &ContinuousCollisionChecker::newGoalCb, this);
    // Notify when the next edge in the current path is clear
    edge_clear_pub_ = nh_.advertise<robo_demo_msgs::JointTrajectoryPointClearStamped>("/edge_clear", 3, true);
  }

  void currentPathCb(const trajectory_msgs::JointTrajectory& current_path)
  {
    current_path_cb_count_++;
    ROS_WARN_STREAM("currentPathCb " << current_path_cb_count_);
    ROS_INFO("Collision detector recieved new path");
    current_path_.clear();

    for (int i=0; i < current_path.points.size(); i++)
      current_path_.emplace_back(current_path.points[i].positions);

    new_edge_ = true;
  }

  void executingToStateCb(const robo_demo_msgs::JointTrajectoryPointStamped::ConstPtr& next_state)
  {
    executing_to_state_cb_count_++;
    ROS_WARN_STREAM("executingToStateCb " << executing_to_state_cb_count_);
    ROS_INFO("Collision detector notified the controller is executing to next state");
    // Check to make sure the next state is as expected
    if (next_state->trajectory_point.positions != current_path_[1])
    {
      ROS_ERROR("Collision detector: recieved unexpected next state from controller");
      ROS_ERROR_STREAM("expected [" << current_path_[1][0] << ", " << current_path_[1][1] << ", " << current_path_[1][2]
                       << ", " << current_path_[1][3] << ", " << current_path_[1][4] << current_path_[1][5] << ", " <<
                       current_path_[1][6] << "]");
      ROS_ERROR_STREAM("recieved [" << next_state->trajectory_point.positions[0] << ", " <<
                       next_state->trajectory_point.positions[1] << ", " << next_state->trajectory_point.positions[2] <<
                       ", " << next_state->trajectory_point.positions[3] << ", " <<
                       next_state->trajectory_point.positions[4] << next_state->trajectory_point.positions[5] << ", " <<
                       next_state->trajectory_point.positions[6] << "]");
      return;
    }
    current_path_.pop_front();
    new_edge_ = true;
  }

  void newGoalCb(const std_msgs::Float64MultiArray::ConstPtr& new_goal_msg)
  {
    // What if the planner gets a new goal first and publishes a new path to the goal and we handle that callback
    // first...potential race
    ROS_INFO("Collision detector recieved new goal");
    if (hasCurrentPath())
      current_path_.clear();
  }

  bool hasCurrentPath()
  {
    if (current_path_.empty() || current_path_.size() == 1)
      return false;
    return true;
  }

  void checkCollision()
  {
    ROS_WARN_THROTTLE(5.0, "Checking collision");
    collision_detection::CollisionRequest req;
    collision_detection::CollisionResult res;
    auto planning_scene = psm_ptr_->getPlanningScene();
    /* auto planning_scene = planning_scene_monitor::LockedPlanningSceneRO(psm_ptr_); */
    planning_scene->setActiveCollisionDetector(collision_detection::CollisionDetectorAllocatorBullet::create(),
                                               true /* exclusive */);
    // Set robot states
    for (int i=0; i<=current_path_[0].size(); i++)
    {
      state1_.setJointPositions("panda_joint" + std::to_string(i+1), &current_path_[0][i]);
      state2_.setJointPositions("panda_joint" + std::to_string(i+1), &current_path_[1][i]);
    }
    state1_.update();
    state2_.update();
    planning_scene->getCollisionEnv()->checkRobotCollision(req, res, state2_, state1_);

    edge_clear_ = !res.collision;
    if (!edge_clear_)
      ROS_ERROR("Detected edge NOT CLEAR");
    if (needToPublish())
    {
      ROS_INFO_STREAM("Collision detector publishing: edge_clear_: " << edge_clear_);
      for (int j=0; j<7; j++)
        ROS_INFO_STREAM("state 1, joint " << (j + 1) << ": " <<
                        *state1_.getJointPositions("panda_joint" + std::to_string(j+1)));
      for (int k=0; k<7; k++)
        ROS_INFO_STREAM("state 2, joint " << (k + 1) << ": " <<
                        *state2_.getJointPositions("panda_joint" + std::to_string(k+1)));
      /* ROS_INFO_STREAM("Collision Detector state1: [" << current_path_[0][0] << ", " << current_path_[0][1] << ", " << */
      /*                 current_path_[0][2] << ", " << current_path_[0][3] << ", " << current_path_[0][4] << ", " << */
      /*                 current_path_[0][5] << ", " << current_path_[0][6] << "]"); */
      /* ROS_INFO_STREAM("Collision Detector state2: [" << current_path_[1][0] << ", " << current_path_[1][1] << ", " << */
      /*                 current_path_[1][2] << ", " << current_path_[1][3] << ", " << current_path_[1][4] << ", " << */
      /*                 current_path_[1][5] << ", " << current_path_[1][6] << "]"); */
      // Publish collision result
      robo_demo_msgs::JointTrajectoryPointClearStamped edge_clear_msg;
      edge_clear_msg.trajectory_point.positions = current_path_[1];
      edge_clear_msg.clear = !res.collision;
      edge_clear_pub_count_++;
      ROS_WARN_STREAM("edge_clear_publication " << edge_clear_pub_count_);
      edge_clear_msg.header.stamp = ros::Time::now();
      edge_clear_pub_.publish(edge_clear_msg);
    }
  }

  bool needToPublish()
  {
    bool publish = false;
    // Want to publish if we are checking a new edge
    if (new_edge_)
    {
      ROS_INFO("Collision detector publishing because NEW_EDGE");
      publish = true;
    }
    // Want to publish if we are checking the same edge as last check
    // and the collision status changed
    else if (edge_clear_ != prev_edge_clear_)
    {
      ROS_INFO("Collision detector publishing because EDGE_CLEAR changed");
      publish = true;
    }
    new_edge_ = false;
    prev_edge_clear_ = edge_clear_;
    return publish;
  }

};


int main(int argc, char** argv)
{
  ros::init(argc, argv, "collision_check_test");
  ros::NodeHandle nh("/collision_check_test");

  ContinuousCollisionChecker ccc(nh);

  while (ros::ok())
  {
    ros::spinOnce();
    if (ccc.hasCurrentPath())
      ccc.checkCollision();
    else
      ROS_WARN_THROTTLE(5.0, "Not checking collision: path to goal not set or on the way to/at goal");
    ros::Duration(0.05).sleep();
  }
}
