#include <ros/ros.h>
#include <control_msgs/FollowJointTrajectoryAction.h>
#include <actionlib/client/simple_action_client.h>
#include <moveit/robot_state/robot_state.h>
#include <moveit/move_group_interface/move_group_interface.h>
#include <moveit/planning_scene_interface/planning_scene_interface.h>
#include <moveit_msgs/DisplayRobotState.h>
#include <moveit_msgs/AttachedCollisionObject.h>
#include <moveit_msgs/CollisionObject.h>
#include <moveit_visual_tools/moveit_visual_tools.h>
#include <memory>

#define CONTROLLER_TOPIC "/position_joint_trajectory_controller/follow_joint_trajectory"
typedef actionlib::SimpleActionClient<control_msgs::FollowJointTrajectoryAction> TrajectoryClient;

class RobotArm
{
private:
  const std::string planning_group_;
  moveit::planning_interface::MoveGroupInterfacePtr move_group_interface_ptr_;
  moveit::planning_interface::PlanningSceneInterface planning_scene_interface_;
  const moveit::core::JointModelGroup* joint_model_group_ptr_;

  TrajectoryClient* trajectory_client_;


public:
  RobotArm(ros::NodeHandle &nh, std::string planning_group)
    : planning_group_(planning_group)
  {
    move_group_interface_ptr_ = std::make_shared<moveit::planning_interface::MoveGroupInterface>(planning_group_);
    joint_model_group_ptr_ = move_group_interface_ptr_->getCurrentState()->getJointModelGroup(planning_group_);

    trajectory_client_ = new TrajectoryClient(CONTROLLER_TOPIC, true);

    while (!trajectory_client_->waitForServer(ros::Duration(5.0))) {
      ROS_INFO_STREAM("Waiting for the " << CONTROLLER_TOPIC << " action server");
    }
  }

  ~RobotArm()
  {
    delete trajectory_client_;
  }

  geometry_msgs::Pose createPoseMsg(double x, double y, double z/*, double w*/)
  {
    geometry_msgs::Pose pose_msg;
    pose_msg.orientation.w = 1.0;
    pose_msg.position.x = x;
    pose_msg.position.y = y;
    pose_msg.position.z = z;
    return pose_msg;
  }

  moveit::planning_interface::MoveGroupInterface::Plan EEPosePlan(geometry_msgs::Pose target_pose)
  {
    move_group_interface_ptr_->setPoseTarget(target_pose);
    moveit::planning_interface::MoveGroupInterface::Plan planned_path;
    bool success = (move_group_interface_ptr_->plan(planned_path)
                    == moveit::planning_interface::MoveItErrorCode::SUCCESS);
    if (!success)
    {
      ROS_ERROR("Error unsuccessful path plan");
      throw;
    }
    return planned_path;
  }

  void executePlan(moveit::planning_interface::MoveGroupInterface::Plan planned_path)
  {
    // Execute trajectory using trajectory_client from MoveGroup plan
    /* move_group_interface_ptr_->asyncExecute(planned_path); */
    // Need to translate from Plan msg -> FollowJointTrajectoryGoal msg
    control_msgs::FollowJointTrajectoryGoal goal_msg;
    goal_msg.trajectory = planned_path.trajectory_.joint_trajectory;
    executeTrajectoryGoal(goal_msg);
  }

  void stopExecution()
  {
    ROS_WARN("Attempting to stop execution");
    // move_group_interface_ptr_->stop(); // OR
    // trajectory_client_->cancelGoal(); // will cause motor shuddering
    moveit::core::RobotStatePtr current_state = move_group_interface_ptr_->getCurrentState(1.0);
    // Instead of above, get current joint values and send a new goal to smoothly stop
    std::vector<double> current_joint_positions = move_group_interface_ptr_->getCurrentJointValues();
    control_msgs::FollowJointTrajectoryGoal stop_goal;
    makeJointGoalMsg(stop_goal, current_joint_positions);
    executeTrajectoryGoal(stop_goal);
  }

  void makeJointGoalMsg(control_msgs::FollowJointTrajectoryGoal &goal, std::vector<double> positions, int index=0,
      double time_from_last=2.0)
  {
    // Resize msg vectors
    goal.trajectory.points.resize(index+1);
    goal.trajectory.points[index].positions.resize(7);
    goal.trajectory.points[index].velocities.resize(7);

    // Time from start by which this state is to be reached
    goal.trajectory.points[index].time_from_start = ros::Duration(time_from_last * (index+1));

    // Fill msg vectors
    for (int i=0; i<7; i++)
    {
      // Add joint names (if this is the first trajectory)
      if (index == 0)
        goal.trajectory.joint_names.push_back("panda_joint" + std::to_string(i+1));
      // Add positions
      goal.trajectory.points[index].positions[i] = positions[i];
      // Add velocities (ALL 0)
      goal.trajectory.points[index].velocities[i] = 0.0;
    }
  }

  control_msgs::FollowJointTrajectoryGoal getSimpleTrajectory()
  {
    std::vector<double> positions{0.252, -0.454, -0.936, -2.078, 1.529, 2.555, 1.966};
    control_msgs::FollowJointTrajectoryGoal goal;
    makeJointGoalMsg(goal, positions);
    // 2nd joint state
    int index = 1;
    positions.clear();
    positions = {-0.546, 0.019, -0.707, -2.150, 1.762, 1.825, 1.739};
    makeJointGoalMsg(goal, positions, index);

    // 3rd joint state
    index = 2;
    positions.clear();
    positions = {0.001, -0.786, 0.000, -2.356, 0.001, 1.572, 0.786};
    makeJointGoalMsg(goal, positions, index);

    return goal;
  }

  void executeTrajectoryGoal(control_msgs::FollowJointTrajectoryGoal goal)
  {
    /* goal.trajectory.header.stamp = ros::Time::now() + ros::Duration(1.0); */
    trajectory_client_->sendGoal(goal);
  }

  actionlib::SimpleClientGoalState getState()
  {
    return trajectory_client_->getState();
  }

};

int main(int argc, char** argv) {
  ros::init(argc, argv, "control_stop_test");
  ros::NodeHandle nh("/control_stop_test");
  ros::AsyncSpinner spinner(1);
  spinner.start();
  RobotArm arm(nh, "panda_arm");
  geometry_msgs::Pose goal_pose = arm.createPoseMsg(0.5, -0.4, 0.6);
  moveit::planning_interface::MoveGroupInterface::Plan planned_path = arm.EEPosePlan(goal_pose);

  control_msgs::FollowJointTrajectoryGoal simple_trajectory = arm.getSimpleTrajectory();
  /* arm.executePlan(planned_path); */
  arm.executeTrajectoryGoal(simple_trajectory);
  ros::Duration(1.0).sleep();
  arm.stopExecution();
  /* ROS_INFO("Wait 3 sec"); */
  /* ros::Duration(3.0).sleep(); */

  /* arm.executeTrajectoryGoal(simple_trajectory); */

  /* ros::Duration(0.5).sleep(); */
  /* arm.stopExecution(); */
  /* ROS_INFO("Wait 3 sec"); */
  /* ros::Duration(3.0).sleep(); */

  /* arm.executeTrajectoryGoal(simple_trajectory); */

  /* ros::Duration(1.0).sleep(); */
  /* arm.stopExecution(); */
  /* ROS_INFO("Wait 3 sec"); */
  /* ros::Duration(3.0).sleep(); */

  /* arm.executeTrajectoryGoal(simple_trajectory); */

  while (!arm.getState().isDone() && ros::ok())
  {
    usleep(10000);
  }
  return 0;
}

// What I really want to do is start planning from a future point, send myself to that point when I want to stop and
// then start up from the plan I already planned when I get there
