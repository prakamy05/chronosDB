#include "src/execution/engine.h"
#include "src/network/tcp_server.h"
#include "src/network/http_dashboard.h"
#include <iostream>

int main() {
    std::cout << "========================================================\n";
    std::cout << "        ChronosDB Relational Storage Engine       \n";
    std::cout << "========================================================\n";

    
    DatabaseClusterManager cluster;

    TCPServer sql_server(5433, [&](std::string query) {
        return cluster.ExecuteClusterWideQuery(query);
    });
    sql_server.Start();

    HTTPDashboard dashboard(8080, 
        [&](std::string query) {
            return cluster.ExecuteClusterWideQuery(query);
        },
        [&](std::string action, std::string payload) {
            return cluster.ProcessDashboardRoute(action, payload);
        }
    );
    dashboard.Start();

    std::cout << "\nInteractive Admin Shell Active:\n";
    while (true) {
        std::cout << "chronosdb [" << cluster.GetActiveDBName() << "]> ";
        std::string line;
        std::getline(std::cin, line);
        if (line == "EXIT" || line == "exit") break;
        if (line.empty()) continue;

        std::string result = cluster.ExecuteClusterWideQuery(line);
        std::cout << result << std::endl;
    }

    std::cout << "[Shutdown Engine] Stopping network boundary processes cleanly...\n";
    sql_server.Stop();
    dashboard.Stop();
    return 0;
}