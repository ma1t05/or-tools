// Copyright 2010-2018 Google LLC
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// [START program]
// [START import]
#include <cmath>
#include <vector>
#include "ortools/constraint_solver/routing.h"
#include "ortools/constraint_solver/routing_enums.pb.h"
#include "ortools/constraint_solver/routing_index_manager.h"
#include "ortools/constraint_solver/routing_parameters.h"
// [END import]

namespace operations_research {
// [START data_model]
struct DataModel {
  DataModel()
      : locations({
            {4, 4},
            {2, 0},
            {8, 0},
            {0, 1},
            {1, 1},
            {5, 2},
            {7, 2},
            {3, 3},
            {6, 3},
            {5, 5},
            {8, 5},
            {1, 6},
            {2, 6},
            {3, 7},
            {6, 7},
            {0, 8},
            {7, 8},
        }),
        num_locations(locations.size()),
        num_vehicles(1),
        depot(0) {
    // Convert locations in meters using a city block dimension of 114m x 80m.
    for (auto& it : locations) {
      const_cast<std::vector<int>&>(it)[0] *= 114;
      const_cast<std::vector<int>&>(it)[1] *= 80;
    }
  }
  const std::vector<std::vector<int>> locations;
  const int num_locations;
  const int num_vehicles;
  const RoutingIndexManager::NodeIndex depot;
};
// [END data_model]

// [START manhattan_distance_matrix]
/*! @brief Generate Manhattan distance matrix.
 * @details It uses the data.locations to computes the Manhattan distance
 * between the two positions of two different indices.*/
std::vector<std::vector<int64>> GenerateManhattanDistanceMatrix(
    const DataModel& data) {
  std::vector<std::vector<int64>> distances = std::vector<std::vector<int64>>(
      data.num_locations, std::vector<int64>(data.num_locations, 0LL));
  for (int fromNode = 0; fromNode < data.num_locations; fromNode++) {
    for (int toNode = 0; toNode < data.num_locations; toNode++) {
      if (fromNode != toNode)
        distances[fromNode][toNode] =
            std::abs(data.locations[toNode][0] - data.locations[fromNode][0]) +
            std::abs(data.locations[toNode][1] - data.locations[fromNode][1]);
    }
  }
  return distances;
}
// [END manhattan_distance_matrix]

// [START solution_printer]
//! @brief Print the solution
//! @param[in] manager Index manager used.
//! @param[in] routing Routing solver used.
//! @param[in] solution Solution found by the solver.
void PrintSolution(const RoutingIndexManager& manager,
                   const RoutingModel& routing, const Assignment& solution) {
  LOG(INFO) << "Objective: " << solution.ObjectiveValue();
  // Inspect solution.
  int64 index = routing.Start(0);
  LOG(INFO) << "Route for Vehicle 0:";
  int64 distance{0};
  std::stringstream route;
  while (routing.IsEnd(index) == false) {
    route << manager.IndexToNode(index).value() << " -> ";
    int64 previous_index = index;
    index = solution.Value(routing.NextVar(index));
    distance += const_cast<RoutingModel&>(routing).GetArcCostForVehicle(
        previous_index, index, int64{0});
  }
  LOG(INFO) << route.str() << manager.IndexToNode(index).value();
  LOG(INFO) << "Distance of the route: " << distance << "m";
  LOG(INFO) << "";
  LOG(INFO) << "Advanced usage:";
  LOG(INFO) << "Problem solved in " << routing.solver()->wall_time() << "ms";
}
// [END solution_printer]

void Tsp() {
  // Instantiate the data problem.
  // [START data]
  DataModel data;
  // [END data]

  // Create Routing Index Manager
  // [START index_manager]
  RoutingIndexManager manager(data.num_locations, data.num_vehicles,
                              data.depot);
  // [END index_manager]

  // Create Routing Model.
  // [START routing_model]
  RoutingModel routing(manager);
  // [END routing_model]

  // Define cost of each arc.
  // [START arc_cost]
  const auto distance_matrix = GenerateManhattanDistanceMatrix(data);
  const int transit_callback_index = routing.RegisterTransitCallback(
      [&distance_matrix, &manager](int64 from_index, int64 to_index) -> int64 {
        return distance_matrix[manager.IndexToNode(from_index).value()]
                              [manager.IndexToNode(to_index).value()];
      });
  routing.SetArcCostEvaluatorOfAllVehicles(transit_callback_index);
  // [END arc_cost]

  // Setting first solution heuristic.
  // [START parameters]
  RoutingSearchParameters searchParameters = DefaultRoutingSearchParameters();
  searchParameters.set_first_solution_strategy(
      FirstSolutionStrategy::PATH_CHEAPEST_ARC);
  // [END parameters]

  // Solve the problem.
  // [START solve]
  const Assignment* solution = routing.SolveWithParameters(searchParameters);
  // [END solve]

  // Print solution on console.
  // [START print_solution]
  PrintSolution(manager, routing, *solution);
  // [END print_solution]
}

}  // namespace operations_research

int main(int argc, char** argv) {
  operations_research::Tsp();
  return EXIT_SUCCESS;
}
// [END program]
