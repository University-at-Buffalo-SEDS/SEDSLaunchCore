from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_application_handoff_uses_non_returning_assembly_tail():
    source = (ROOT / "bootloader/src/boot_jump.c").read_text()
    disable = source.index("launchcore_cortex_disable_irq()")
    tail = source.index("launchcore_cortex_start_application(app_sp, app_pc)")

    assert disable < tail
    assert "__attribute__((naked, noreturn))" in source
    assert '"msr msp, r0\\n"' in source
    assert '"cpsie i\\n"' in source
    assert '"bx r1\\n"' in source
    assert "launchcore_cortex_set_msp(app_sp)" not in source
