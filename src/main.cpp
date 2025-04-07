#include <uWS/uWS.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "helpers.h"
#include "json.hpp"
#include "spline.h"
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>

// for convenience
using nlohmann::json;
using std::string;
using std::vector;

int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Start in lane 1, which is the center lane (0 is left and 2 is right)
  int lane = 1;
  // Start at zero velocity and gradually accelerate
  double ref_vel = 0.0; // mph

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  std::ifstream in_map_(map_file_.c_str(), std::ifstream::in);

  string line;
  while (getline(in_map_, line)) {
    std::istringstream iss(line);
    double x;
    double y;
    float s;
    float d_x;
    float d_y;
    iss >> x;
    iss >> y;
    iss >> s;
    iss >> d_x;
    iss >> d_y;
    map_waypoints_x.push_back(x);
    map_waypoints_y.push_back(y);
    map_waypoints_s.push_back(s);
    map_waypoints_dx.push_back(d_x);
    map_waypoints_dy.push_back(d_y);
  }

  h.onMessage([&map_waypoints_x,&map_waypoints_y,&map_waypoints_s,
               &map_waypoints_dx,&map_waypoints_dy,&ref_vel,&lane]
              (uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
               uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
          // Main car's localization Data
          double car_x = j[1]["x"];
          double car_y = j[1]["y"];
          double car_s = j[1]["s"];
          double car_d = j[1]["d"];
          double car_yaw = j[1]["yaw"];
          double car_speed = j[1]["speed"];

          // Previous path data given to the Planner
          auto previous_path_x = j[1]["previous_path_x"];
          auto previous_path_y = j[1]["previous_path_y"];
          // Previous path's end s and d values 
          double end_path_s = j[1]["end_path_s"];
          double end_path_d = j[1]["end_path_d"];

          // Sensor Fusion Data, a list of all other cars on the same side 
          //   of the road.
          auto sensor_fusion = j[1]["sensor_fusion"];

          json msgJson;

          /**
           * The given code defines a path made up of (x,y) points that the car
           *   will visit sequentially every .02 seconds
           */

          int prev_size = previous_path_x.size();
          
          if (prev_size > 0) {
            car_s = end_path_s;
          }
        
          bool too_close = false; // True if too close to a car in front
          vector<int> availLanes = getAvailLanes(lane);
          vector<double> nextLaneClosestFrontDist(3, 1000);
          vector<double> nextLaneClosestRearDist(3, 1000);
          double curLaneClosestDist = 1000;

          // Compute closest s coord distances between ego and cars in each lane. Find ref_v to use
          for (int i = 0; i < sensor_fusion.size(); i++) {
            // Check if the car is in the same lane as the ego vehicle
            float d = sensor_fusion[i][6];
            int check_lane = getLaneOfCar(d); // lane of the car we want to check

            double vx = sensor_fusion[i][3];
            double vy = sensor_fusion[i][4];
            double check_speed = sqrt(vx*vx + vy*vy);
            double check_car_s = sensor_fusion[i][5];
            
            // Calculate the check_car's future location
            check_car_s += (double)prev_size * 0.02 * check_speed;
            if (check_car_s >= car_s && (check_car_s - car_s) < nextLaneClosestFrontDist[check_lane]){ // car is in front of ego
              nextLaneClosestFrontDist[check_lane] = check_car_s - car_s;
            } else if (check_car_s < car_s && (car_s - check_car_s) < nextLaneClosestRearDist[check_lane]){ // car is behind ego
              nextLaneClosestRearDist[check_lane] = car_s - check_car_s;
            }             
          }
          
          // ---------- select optimal lane ---------------
          int optimalLane = lane;

          // bestLaneGap: The best lane is the one with the biggest "gap". 
          // We call the "gap" the sum of the distance in front and behind the ego car.
          double bestLaneGap = 0; 

          // Only consider switching lanes if we're getting too close to front car in same lane.
          // Also check if the car is centered in lane, meaning it isn't currently trying to change lanes.
          //  If there is already a lane change in progress, we don't want to interrupt it and make another lane change
          vector<double> laneGap(3, 0);
          if (nextLaneClosestFrontDist[lane] < 30) { 
            too_close = true;
            if (isCenteredInLane(car_d, lane)) {
              for (int i : availLanes) {
                double frontGap = nextLaneClosestFrontDist[i];
                double rearGap = nextLaneClosestRearDist[i];
                if (frontGap > 30 && rearGap > 25) {
                  laneGap[i] = frontGap + rearGap;
                  if ((1.2*frontGap + rearGap) > bestLaneGap) {
                    optimalLane = i;
                    bestLaneGap = 1.2*frontGap + rearGap;
                  }
                }
              }

              bool needTieBreaker = false;
              for (int i : availLanes) {
                double frontGap = nextLaneClosestFrontDist[i];
                double rearGap = nextLaneClosestRearDist[i]; 
                if (i != optimalLane && abs(laneGap[i] - bestLaneGap) < 30 &&
                    frontGap > 30 && rearGap > 25) {
                  needTieBreaker = true;
                }
              }

              if (needTieBreaker) {
                std::cout << "needTieBreaker " << optimalLane << std::endl;
                double bestNeighbourGap = 0;
                for (int i=0; i < 3; ++i) {
                  vector<int> availLanesForI = getAvailLanes(i);
                  for (int l : availLanesForI) {
                    double frontGap = nextLaneClosestFrontDist[l];
                    double rearGap = nextLaneClosestRearDist[l]; 
                    if ((1.2*frontGap + rearGap) - bestNeighbourGap > 50 &&
                        frontGap > 30 && rearGap > 25) {
                      bestNeighbourGap = 1.2*frontGap + rearGap;
                      optimalLane = i;
                    }
                  }
                }
                std::cout << "done needTieBreaker " << optimalLane << std::endl;

              }
            }
          }

          lane = optimalLane;
        
          // Create a list of evenly spaced waypoints 30m apart
          // Interpolate those waypoints later with spline and fill it in with more points
          vector<double> ptsx;
          vector<double> ptsy;
        
          // Reference x, y, yaw states, either will be the starting point or end point of the previous path
          double ref_x = car_x;
          double ref_y = car_y;
          double ref_yaw = deg2rad(car_yaw);
        
          // if previous size is almost empty, use the car as starting reference
          if (prev_size < 2) {
            // Use two points that make the path tangent to the car
            double prev_car_x = car_x - 0.5 * cos(car_yaw);
            double prev_car_y = car_y - 0.5 * sin(car_yaw);
            ptsx.push_back(prev_car_x);
            ptsx.push_back(car_x);
            ptsy.push_back(prev_car_y);
            ptsy.push_back(car_y);
          }
          // Use the previous path's end point as starting reference
          else {
            ref_x = previous_path_x[prev_size-1];
            ref_y = previous_path_y[prev_size-1];
            double ref_x_prev = previous_path_x[prev_size-2];
            double ref_y_prev = previous_path_y[prev_size-2];
            ref_yaw = atan2(ref_y - ref_y_prev , ref_x - ref_x_prev);
            // Use the two points that make the path tangent to the previous path's end point
            ptsx.push_back(ref_x_prev);
            ptsx.push_back(ref_x);
            ptsy.push_back(ref_y_prev);
            ptsy.push_back(ref_y);
          }
        
          // Add evenly 30m spaced points ahead of the starting reference
          vector<double> next_wp0 = getXY(car_s+30, 2+4*lane, map_waypoints_s, map_waypoints_x, map_waypoints_y);
          vector<double> next_wp1 = getXY(car_s+60, 2+4*lane, map_waypoints_s, map_waypoints_x, map_waypoints_y);
          vector<double> next_wp2 = getXY(car_s+90, 2+4*lane, map_waypoints_s, map_waypoints_x, map_waypoints_y);
          ptsx.push_back(next_wp0[0]);
          ptsx.push_back(next_wp1[0]);
          ptsx.push_back(next_wp2[0]);
          ptsy.push_back(next_wp0[1]);
          ptsy.push_back(next_wp1[1]);
          ptsy.push_back(next_wp2[1]);
        
          for (int i = 0; i < ptsx.size(); i++) {
            // shift car reference angle to 0 degrees
            double shift_x = ptsx[i]-ref_x;
            double shift_y = ptsy[i]-ref_y;
            ptsx[i] = shift_x * cos(0-ref_yaw) - shift_y * sin(0-ref_yaw);
            ptsy[i] = shift_x * sin(0-ref_yaw) + shift_y * cos(0-ref_yaw);
          }
        
          // Create a spline
          tk::spline s;
          // Set (x,y) points to the spline (i.e. fits a spline to those points)
          s.set_points(ptsx, ptsy);
          
          // Define the actual (x,y) points we will use for the planner
          vector<double> next_x_vals;
          vector<double> next_y_vals;

          // Start with all of the previous path points from last time
          for (int i = 0; i < previous_path_x.size(); i++) {
            next_x_vals.push_back(previous_path_x[i]);
            next_y_vals.push_back(previous_path_y[i]);
          }
          // Calculate how to break up spline points so that we travel at desired velocity
          double target_x = 30.0; // 30.0 m is the distance horizon
          double target_y = s(target_x);
          double target_dist = sqrt(target_x*target_x + target_y*target_y); // this is the d in the diagram
          double x_add_on = 0.0; // Related to the transformation (starting at zero)
          // Fill up the rest of path planner after filling it with previous points, will always output 50 points
          for (int i = 1; i <= 50-previous_path_x.size(); i++) {
            // Reduce speed if too close, add if no longer close
            if (too_close) {
              ref_vel -= .224;
            } else if (ref_vel < 49.5) {
              ref_vel += .224;
            }
            double N = (target_dist/(0.02*ref_vel/2.24));
            double x_point = x_add_on + target_x/N;
            double y_point = s(x_point);
            
            x_add_on = x_point;
            
            double x_ref = x_point;
            double y_ref = y_point;
            
            // Rotate x, y back to normal
            x_point = x_ref * cos(ref_yaw) - y_ref * sin(ref_yaw);
            y_point = x_ref * sin(ref_yaw) + y_ref * cos(ref_yaw);
            
            x_point += ref_x;
            y_point += ref_y;
            
            next_x_vals.push_back(x_point);
            next_y_vals.push_back(y_point);
          }

          msgJson["next_x"] = next_x_vals;
          msgJson["next_y"] = next_y_vals;

          auto msg = "42[\"control\","+ msgJson.dump()+"]";

          ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
        }  // end "telemetry" if
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }  // end websocket if
  }); // end h.onMessage

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  
  h.run();
}
