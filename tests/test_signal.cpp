#include <nuf/document/Signal.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Signal notifica a todos los receptores conectados", "[signal]")
{
    nuf::Signal<int> signal;
    int sum = 0;
    signal.connect([&](int value) { sum += value; });
    signal.connect([&](int value) { sum += value * 10; });

    signal.notify(2);

    REQUIRE(sum == 22);
}

TEST_CASE("Signal deja de notificar tras desconectar", "[signal]")
{
    nuf::Signal<> signal;
    int calls = 0;
    const auto id = signal.connect([&] { ++calls; });

    signal.notify();
    signal.disconnect(id);
    signal.notify();

    REQUIRE(calls == 1);
    REQUIRE(signal.connectionCount() == 0);
}

TEST_CASE("Un receptor puede desconectarse durante la notificación", "[signal]")
{
    nuf::Signal<> signal;
    nuf::Signal<>::Connection id = 0;
    int calls = 0;
    id = signal.connect([&] {
        ++calls;
        signal.disconnect(id);
    });

    signal.notify();
    signal.notify();

    REQUIRE(calls == 1);
}
