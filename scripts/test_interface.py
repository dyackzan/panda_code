#!/usr/bin/env python3

import json
import os
import numpy as np
import pandas as pd
import sys
import threading as th

import rospy
import rospkg
from std_msgs.msg import Float64MultiArray
from std_msgs.msg import Bool

from demo_interface import DemoInterface
from RRT_dynamic_interface import RRTPlannerControlInterface

rospack = rospkg.RosPack()
EE_CONTROL_PATH = rospack.get_path('end_effector_control')
PLANNING_DATA_PATH = os.path.join(EE_CONTROL_PATH, 'data', 'planning')
SUMMARY_COLUMNS = ["Test", "RRTstar cost", "RTRRTstar cost", "Difference"]
START_STATE = [0, -0.785, 0, -2.356, 0, 1.571, 0.785]
INITIAL_JOINT_GOAL = [0.9022525451956217, -1.0005812062660042, -1.7602947518592436,
                      -2.7099525294963933, -0.1420045228964755, 3.493668294307763,
                      -0.3472854722693375]
INITIAL_JOINT_GOAL2 = [-0.5578778636322044, 0.908623569993187, -0.38844591131009487,
                       -1.2094721531006272, 0.3444179383032919, 2.0541426810783356,
                       -0.18421686175609792]
FINAL_JOINT_GOAL = [1.1901980109241104, 0.9615559746057705, -0.5881359185350531,
                    -1.2015471132200233, 0.5281574185640393, 2.0160775068768824,
                    1.3658315499054479]

class TestInterface():

    def __init__(self):
        self.d = DemoInterface(node_initialized=True)
        rospy.sleep(0.5)
        self.init_pubs()
        self.get_joint_goals()

    def get_joint_goals(self):
        self.start_state = rospy.get_param("/test_interface/start_state", START_STATE)
        self.initial_joint_goal = rospy.get_param("/test_interface/initial_joint_goal",
                                                  INITIAL_JOINT_GOAL)
        self.final_joint_goal = rospy.get_param("/test_interface/final_joint_goal",
                                                FINAL_JOINT_GOAL)

    def init_pubs(self):
        self.new_goal_pub = rospy.Publisher("/new_planner_goal", Float64MultiArray, queue_size=1)
        self.obstacles_changed_pub = rospy.Publisher("/obstacles_changed", Bool, queue_size=1)

    def run_test(self, test_type):
        if test_type == "change_goal":
            dyn_time = rospy.get_param("/test_interface/dyn_time", 1.75)
            rospy.loginfo("Running change_goal test")
            self.run_change_goal_test(dyn_time)
        elif test_type == "add_obstacle":
            dyn_time = rospy.get_param("/test_interface/dyn_time", 1.0)
            rospy.loginfo("Running add_obstacle test")
            self.run_add_obstacle_test(dyn_time)
        elif test_type == "add_obstacle_change_goal":
            dyn_time = rospy.get_param("/test_interface/dyn_time", 2.0)
            self.initial_joint_goal = rospy.get_param("/test_interface/initial_joint_goal",
                                                      INITIAL_JOINT_GOAL2)
            rospy.loginfo("Running add_obstacle_change_goal test")
            self.run_add_obstacle_change_goal_test(dyn_time)
        else:
            rospy.logerr(f"Invalid test_type: {test_type}")

    def setup(self, planner, start=[]):
        rospy.loginfo("Setting up")
        self.d.remove_object("obstacle")
        self.object_names = self.d.scene.get_known_object_names()
        # for object_name in self.object_names:
        #     self.d.remove_object(object_name)
        self.d.set_planner_id("RRTstarkConfigRealTimeTesting")
        if start:
            rospy.loginfo(f"Going to provided start: {start}")
            self.d.go_to_joint_goal(start)
        else:
            rospy.loginfo("Going to start state")
            self.d.go_to_joint_goal(self.start_state)
        self.d.set_planner_id(planner)

    def run_change_goal_test(self, dyn_time):
        rospy.loginfo("Running RTRRT change_goal test")
        self.run_rtrrt_change_goal_test(dyn_time)
        rospy.loginfo("Running RRT change_goal test")
        self.run_rrt_change_goal_test()
        rospy.loginfo("Saving summary for change_goal test")
        self.update_summary("change_goal")

    def run_rtrrt_change_goal_test(self, dyn_time):
        self.setup("RTRRTstarkConfigDefault")
        final_joint_msg = Float64MultiArray()
        final_joint_msg.data = self.final_joint_goal
        log_timer = th.Timer(dyn_time, rospy.loginfo, args=("Sending final joint goal",))
        change_goal_timer = th.Timer(dyn_time, self.new_goal_pub.publish, args=(final_joint_msg,))
        log_timer.start()
        change_goal_timer.start()
        rospy.loginfo("Sending initial joint goal")
        self.d.plan_to_joint_goal(self.initial_joint_goal)

    def run_rrt_change_goal_test(self):
        (start_state, goal_state) = self.load_start_and_goal_states()
        self.setup("RRTstarkConfigRealTimeTesting", start=start_state)
        rospy.loginfo("Sending joint goal")
        self.d.go_to_joint_goal(goal_state)

    def run_add_obstacle_test(self, dyn_time):
        rospy.loginfo("Running RTRRT add_obstacle test")
        self.run_rtrrt_add_obstacle_test(dyn_time)
        rospy.loginfo("Running RRT add_obstacle test")
        self.run_rrt_add_obstacle_test()
        rospy.loginfo("Saving summary for add_obstacle test")
        self.update_summary("add_obstacle")

    def run_rtrrt_add_obstacle_test(self, dyn_time):
        self.setup("RTRRTstarkConfigDefault")
        (x, y, z, r) = (0.4, -0.4, 0.4, 0.05)
        log_timer = th.Timer(dyn_time, rospy.loginfo, args=("Adding new obstacle now",))
        add_obstacle_timer = th.Timer(dyn_time, self.d.publish_object_manual, ("obstacle", x, y, z,
                                                                          r, 'sphere'))
        log_timer.start()
        add_obstacle_timer.start()
        rospy.loginfo("Sending initial joint goal")
        self.d.plan_to_joint_goal(self.initial_joint_goal)

    def run_rrt_add_obstacle_test(self):
        (start_state, goal_state) = self.load_start_and_goal_states()
        self.setup("RRTstarkConfigRealTimeTesting", start=start_state)
        (x, y, z, r) = (0.4, -0.4, 0.4, 0.05)
        self.d.publish_object_manual("obstacle", x, y, z, r, type='sphere')
        rospy.loginfo("Sending joint goal")
        self.d.go_to_joint_goal(goal_state)

    def run_add_obstacle_change_goal_test(self, dyn_time):
        rospy.loginfo("Running RTRRT add_obstacle_change_goal test")
        self.run_rtrrt_add_obstacle_change_goal_test(dyn_time)
        rospy.loginfo("Running RRT add_obstacle_change_goal test")
        self.run_rrt_add_obstacle_change_goal_test()
        rospy.loginfo("Saving summary for add_obstacle_change_goal test")
        self.update_summary("add_obstacle_change_goal")

    def run_rtrrt_add_obstacle_change_goal_test(self, dyn_time):
        self.setup("RTRRTstarkConfigDefault")
        final_joint_msg = Float64MultiArray()
        final_joint_msg.data = self.final_joint_goal
        (x, y, z, size) = (0.8, 0.0, 0.4, (0.5, 0.02, 0.4))
        log_timer = th.Timer(2.0, rospy.loginfo, args=("Adding obstacle, sending final joint "
                                                       "goal"))
        add_obstacle_timer = th.Timer(dyn_time, self.d.publish_object_manual, ("obstacle", x, y, z,
                                                                          size))
        change_goal_timer = th.Timer(dyn_time, self.new_goal_pub.publish, args=(final_joint_msg,))
        log_timer.start()
        add_obstacle_timer.start()
        change_goal_timer.start()
        rospy.loginfo("Sending initial joint goal")
        self.d.plan_to_joint_goal(self.initial_joint_goal)

    def run_rrt_add_obstacle_change_goal_test(self):
        (start_state, goal_state) = self.load_start_and_goal_states()
        self.setup("RRTstarkConfigRealTimeTesting", start=start_state)
        (x, y, z, size) = (0.4, 0.0, 0.4, (0.5, 0.02, 0.5))
        rospy.loginfo("Adding obstacle")
        self.d.publish_object_manual("obstacle", x, y, z, size)
        rospy.loginfo("Sending joint goal")
        self.d.go_to_joint_goal(goal_state)

    def load_start_and_goal_states(self):
        with open(os.path.join(PLANNING_DATA_PATH, 'RTRRTstar_run.json'), 'r') as f:
            RTRRT_json = json.load(f)
        num_goals = len(RTRRT_json['Goals'])
        start_state = RTRRT_json['Goal' + str(num_goals) + 'States'][0]
        last_goal_state = RTRRT_json['Goals'][-1]
        return (start_state, last_goal_state)

    def update_summary(self, test):
        (RRTstar_cost, RTRRTstar_cost, diff) = self.calculate_run_costs()
        run_db = pd.DataFrame([[test, RRTstar_cost, RTRRTstar_cost, diff]], columns=SUMMARY_COLUMNS)
        REPORT_PATH = os.path.join(PLANNING_DATA_PATH, 'summary.csv')
        if os.path.exists(REPORT_PATH):
            summary_db = pd.read_csv(REPORT_PATH)
        else:
            summary_db = pd.DataFrame(columns=SUMMARY_COLUMNS)
        summary_db = pd.concat([summary_db, run_db])
        summary_db.to_csv(REPORT_PATH, index=False)

    def calculate_run_costs(self):
        with open(os.path.join(PLANNING_DATA_PATH, 'RRTstar_run.json'), 'r') as f:
            RRT_json = json.load(f)
        with open(os.path.join(PLANNING_DATA_PATH, 'RTRRTstar_run.json'), 'r') as f:
            RTRRT_json = json.load(f)
        RRT_cost = self.calculate_cost(RRT_json['States'])
        # Extract RTRRT cost from second leg
        num_goals = len(RTRRT_json.items())-1
        last_goal_states_key = "Goal" + str(num_goals) + "States"
        RTRRT_states = RTRRT_json[last_goal_states_key]
        RTRRT_cost = self.calculate_cost(RTRRT_states)
        return (RRT_cost, RTRRT_cost, (RRT_cost - RTRRT_cost))

    def calculate_cost(self, states):
        cost = 0
        for i in range(len(states) - 1):
            s1 = states[i]
            s2 = states[i + 1]
            for j in range(len(states[i])):
                    diff = s1[j] - s2[j]
                    cost += diff ** 2
        return np.sqrt(cost)


if __name__ == "__main__":
    rospy.init_node("test_interface")
    ti = TestInterface()
    test_type = rospy.get_param('/test_interface/test_type', 'change_goal')
    ti.run_test(test_type)