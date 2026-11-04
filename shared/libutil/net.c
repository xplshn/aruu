#include "../net.h"
#include "../util.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>

int
net_show_routes(void)
{
  struct NetRoute *routes = NULL;
  char             dest[NET_ROUTE_ADDRSTRLEN];
  char             gateway[NET_ROUTE_ADDRSTRLEN];
  char             mask[NET_ROUTE_ADDRSTRLEN];
  int              count = 0;
  int              i;

  if (net_get_routes(&routes, &count) < 0)
    return -1;

  printf(
      "%-10s %-15s %-15s %-15s %-6s %-6s\n",
      "Iface",
      "Destination",
      "Gateway",
      "Genmask",
      "Flags",
      "Metric"
  );
  for (i = 0; i < count; i++) {
    strlcpy(dest, inet_ntoa(routes[i].destination), sizeof(dest));
    strlcpy(gateway, inet_ntoa(routes[i].gateway), sizeof(gateway));
    strlcpy(mask, inet_ntoa(routes[i].mask), sizeof(mask));
    printf(
        "%-10s %-15s %-15s %-15s 0x%04X %-6u\n",
        routes[i].interface,
        dest,
        gateway,
        mask,
        routes[i].flags,
        routes[i].metric
    );
  }
  free(routes);
  return 0;
}
