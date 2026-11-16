#include <logic/CounterWire.h>

#include <gameplay_tags/GameplayTagWire.h>

void ReplicationCodec<Counter>::ToWire(const ReplicationWireContext& context, const Counter& counter,
                                       CounterWire& wire)
{
    wire = CounterWire{ .Name = WireTag(context, counter.Name), .Value = counter.Value };
}

void ReplicationCodec<Counter>::FromWire(const ReplicationWireContext& context, const CounterWire& wire,
                                         Counter& counter)
{
    counter = Counter{ .Name = LocalTag(context, wire.Name), .Value = wire.Value };
}
