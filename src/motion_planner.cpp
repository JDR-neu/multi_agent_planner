#include <ros/ros.h>
#include <visualization_msgs/Marker.h>
#include <geometry_msgs/Point.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <climits>
#include "multi_agent_planner/get_plan.h"
#include "multi_agent_planner/agent_info.h"

using std::vector;

struct Path
{
    std::string serial_id;
    vector<geometry_msgs::Point> point_list;
};

enum Status {FREE, OCCUPIED, START, GOAL};

struct Grid_node
{
    Status stat;
    bool is_closed;
    int past_cost;
    int total_cost;
    int pos[2];
    geometry_msgs::Point parent;

    bool operator<(Grid_node other) const
    {
        return total_cost < other.total_cost;
    }
};

class Motion_Planner
{
public:
    Motion_Planner(ros::NodeHandle *node_handle, int X_max = 10, int Y_max = 10, int edge_cost = 10);
private:
    ros::NodeHandle node;
    ros::Publisher pub_marker;
    ros::Subscriber sub_agent_pose;
    ros::ServiceServer srv_get_plan;
    void create_roadmap();
    bool get_plan(multi_agent_planner::get_plan::Request &req, multi_agent_planner::get_plan::Response &res);
    void agent_start_pose_callback(const multi_agent_planner::agent_info &msg);
    const int X_MAX;
    const int Y_MAX;
    const int edge_cost;
    vector<Path> archived_paths;
    vector<multi_agent_planner::agent_info> agent_start_poses;
};

Motion_Planner::Motion_Planner(ros::NodeHandle *node_handle, int X_max, int Y_max, int edge_cost)
    : node(*node_handle), X_MAX(X_max), Y_MAX(Y_max), edge_cost(edge_cost)
{
    pub_marker = node.advertise<visualization_msgs::Marker>("visualization_marker", 1);
    sub_agent_pose = node.subscribe("/agent_feedback", 100, &Motion_Planner::agent_start_pose_callback, this);
    srv_get_plan = node.advertiseService("/get_plan", &Motion_Planner::get_plan, this);

    create_roadmap();
}

bool Motion_Planner::get_plan(multi_agent_planner::get_plan::Request &req, multi_agent_planner::get_plan::Response &res)
{
    geometry_msgs::Pose2D start_pose, goal_pose;
    vector<geometry_msgs::Point> path_list;
    goal_pose = req.goal_pose;
    bool found = false;
    for (auto agent : agent_start_poses)
    {
        if (agent.serial_id == req.serial_id)
        {
            start_pose = agent.start_pose;
            found = true;
            break;
        }
    }
    if (!found)
    {
        ROS_ERROR("That agent does not yet exist.");
    }

    geometry_msgs::Point start_point, goal_point;
    start_point.x = start_pose.x;
    start_point.y = start_pose.y;
    goal_point.x = goal_pose.x;
    goal_point.y = goal_pose.y;
    geometry_msgs::Point *start_pntr {nullptr};
    geometry_msgs::Point *goal_pntr {nullptr};
    bool archived_start_found = false;
    bool archived_goal_found = false;
    for (auto &path_obj : archived_paths)
    {
        for (auto &point : path_obj.point_list)
        {
            if (point.x == start_point.x && point.y == start_point.y && !archived_start_found)
            {
                ROS_INFO("Found starting point in an archived path originally created for %s", path_obj.serial_id.c_str());
                start_pntr = &point;
                archived_start_found = true;
            }
            if (point.x == goal_point.x && point.y == goal_point.y && !archived_goal_found)
            {
                ROS_INFO("Found goal point in an archived path originally created for %s", path_obj.serial_id.c_str());
                goal_pntr = &point;
                archived_goal_found = true;
            }
            if (archived_start_found && archived_goal_found)
            {
                break;
            }
        }
        if (archived_start_found && archived_goal_found)
        {
            break;
        }
    }

    if (start_pntr != nullptr && goal_pntr != nullptr)
    {
        vector<geometry_msgs::Point>::iterator start_it(start_pntr);
        vector<geometry_msgs::Point>::iterator goal_it(goal_pntr);
        if (start_pntr < goal_pntr)
        {
            goal_it++;
            vector<geometry_msgs::Point> sub_path(start_it, goal_it);
            res.path = sub_path;
        }
        else
        {
            start_it++;
            vector<geometry_msgs::Point> sub_path(goal_it, start_it);
            std::reverse(sub_path.begin(), sub_path.end());
            res.path = sub_path;
        }
        ROS_INFO("Using archived path");
        return true;
    }

    ROS_INFO("Using A* algorithm as complete archived path could not be found");
    // A* algorithm
    vector<Grid_node> open;
    Path final_path {};
    final_path.serial_id = req.serial_id;
    Grid_node grid[X_MAX][Y_MAX] = {};
    for (size_t i{0}; i < X_MAX; i++)
    {
        for (size_t j{0}; j < Y_MAX; j++)
        {
            Grid_node n = {};
            n.stat = FREE;
            n.pos[0] = i;
            n.pos[1] = j;
            n.past_cost = INT_MAX;
            grid[i][j] = n;
        }
    }

    int start[] = {(int)start_pose.x, (int)start_pose.y};
    int goal[] = {(int)goal_pose.x, (int)goal_pose.y};
    grid[start[0]][start[1]].stat = START;
    grid[start[0]][start[1]].past_cost = 0;
    grid[goal[0]][goal[1]].stat = GOAL;
    int x_curr{}, y_curr{};
    open.push_back(grid[start[0]][start[1]]);

    int x_nbr_arr[] = {0, 1, 0, -1};
    int y_nbr_arr[] = {1, 0, -1, 0};
    int x_nbr{}, y_nbr{};
    Grid_node curr{};

    while (open.size() != 0)
    {
        curr = open.at(0);
        x_curr = curr.pos[0];
        y_curr = curr.pos[1];
        open.erase(open.begin());
        grid[x_curr][y_curr].is_closed = true;
        if (grid[x_curr][y_curr].stat == GOAL)
        {
            geometry_msgs::Point goal;
            goal.x = x_curr;
            goal.y = y_curr;
            final_path.point_list.push_back(goal);
            while (x_curr != start[0] || y_curr != start[1])
            {
                final_path.point_list.insert(final_path.point_list.begin(), grid[x_curr][y_curr].parent);
                x_curr = final_path.point_list.at(0).x;
                y_curr = final_path.point_list.at(0).y;
            }
            archived_paths.push_back(final_path);
            res.path = final_path.point_list;
            ROS_INFO("Algorithm found a path");
            return true;
        }
        for (size_t i{0}; i < 4; i++)
        {
            x_nbr = x_curr + x_nbr_arr[i];
            y_nbr = y_curr + y_nbr_arr[i];
            if (x_nbr >= 0 && x_nbr < X_MAX && y_nbr >= 0 && y_nbr < Y_MAX)
            {
                if (!grid[x_nbr][y_nbr].is_closed && grid[x_nbr][y_nbr].stat != OCCUPIED)
                {
                    int tentative_past_cost = curr.past_cost + edge_cost;
                    if (tentative_past_cost < grid[x_nbr][y_nbr].past_cost)
                    {
                        grid[x_nbr][y_nbr].past_cost = tentative_past_cost;
                        grid[x_nbr][y_nbr].parent.x = x_curr;
                        grid[x_nbr][y_nbr].parent.y = y_curr;
                        grid[x_nbr][y_nbr].total_cost = grid[x_nbr][y_nbr].past_cost + edge_cost*(abs(x_nbr - goal[0]) + abs(y_nbr - goal[1]));
                        open.push_back(grid[x_nbr][y_nbr]);
                    }
                }
            }
        }
        std::sort(open.begin(), open.end());
    }

    ROS_INFO("Failed");
    return false;
}

void Motion_Planner::agent_start_pose_callback(const multi_agent_planner::agent_info &msg)
{
    bool found = false;
    for (size_t i{0}; i < agent_start_poses.size(); i++)
    {
        if (agent_start_poses.at(i).serial_id == msg.serial_id)
        {
            agent_start_poses.at(i) = msg;
            found = true;
            break;
        }
    }
    if (!found)
    {
        agent_start_poses.push_back(msg);
    }
}

void Motion_Planner::create_roadmap()
{
    visualization_msgs::Marker marker;
    marker.header.frame_id = "/world";
    marker.header.stamp = ros::Time();
    marker.ns = "sphere_list";
    marker.id = 0;
    marker.type = visualization_msgs::Marker::SPHERE_LIST;
    marker.action = visualization_msgs::Marker::ADD;
    marker.pose.orientation.w = 1.0;
    marker.scale.x = 0.25;
    marker.scale.y = 0.25;
    marker.scale.z = 0.25;
    marker.color.r = 1.0;
    marker.color.a = 1.0;
    marker.lifetime = ros::Duration();

    for (size_t i {0}; i <= 10; i++)
        for (size_t j{0}; j <= 10; j++)
        {
            geometry_msgs::Point p;
            p.x = i;
            p.y = j;
            marker.points.push_back(p);
        }
    while (pub_marker.getNumSubscribers() < 1)
     {
       if (!ros::ok())
       {
         break;
       }
       sleep(1);
     }
    pub_marker.publish(marker);
}

int main( int argc, char** argv )
{
    ros::init(argc, argv, "nodes");
    ros::NodeHandle n;
    Motion_Planner mp(&n);
    ros::spin();
    return 0;
}
