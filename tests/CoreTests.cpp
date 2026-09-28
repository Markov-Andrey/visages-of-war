#include "TestSupport.hpp"

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const rts::tests::TestContext context(argv[1]);
    rts::tests::TestSuite suite;
    rts::tests::navigationTests(suite, context);
    rts::tests::economyTests(suite, context);
    rts::tests::combatTests(suite, context);
    rts::tests::projectileTests(suite, context);
    rts::tests::dataTests(suite, context);
    rts::tests::visionTests(suite, context);
    rts::tests::selectionTests(suite, context);
    rts::tests::formationTests(suite, context);
    rts::tests::movementTests(suite, context);
    rts::tests::editorTests(suite, context);
    std::cout << suite.passed << " passed, " << suite.failures << " failed\n";
    return suite.failures ? 1 : 0;
}
