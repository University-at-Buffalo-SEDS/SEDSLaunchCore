from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_application_handoff_restores_reset_interrupt_state():
    source = (ROOT / "bootloader/src/boot_jump.c").read_text()
    disable = source.index("launchcore_cortex_disable_irq()")
    enable = source.index("launchcore_cortex_enable_irq()")
    branch = source.index("((launchcore_entry_t)app_pc)()")

    assert disable < enable < branch
