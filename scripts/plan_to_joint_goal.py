#!/usr/bin/env python3

import sys
import yaml

from demo_interface import DemoInterface

def parse_joint_goal(filename):
    with open(filename, "r") as f:
        return yaml.safe_load(f)

if __name__ == "__main__":
    d = DemoInterface()
    joint_goal = []
    if len(sys.argv) > 1:
        joint_goal = parse_joint_goal(sys.argv[1])
    else:
        for i in range(7):
            joint_goal.append(float(input(f"Input joint goal for joint {i}: ")))
    print(f"Joint goal: {joint_goal}")
    d.plan_to_joint_goal(joint_goal)