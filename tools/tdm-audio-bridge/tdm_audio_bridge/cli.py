"""CLI for the TDM Audio Bridge companion tool.

Connects to the TDM Audio Stream HLA's TCP server and plays decoded
audio through a local audio device or virtual sound card.
"""

import logging
import signal
import sys
import threading

import click

from .client import StreamClient
from .player import Player, list_output_devices, find_device

try:
    from ._version import VERSION as __version__
except ImportError:
    __version__ = 'unknown'

log = logging.getLogger('tdm-audio-bridge')


@click.group()
@click.option('-v', '--verbose', count=True,
              help='Increase verbosity (-v info, -vv debug)')
@click.version_option(__version__, '-V', '--version')
@click.help_option('-h', '--help')
@click.pass_context
def cli(ctx, verbose):
    """TDM Audio Bridge — play decoded TDM audio in real time.

    Connects to the TDM Audio Stream HLA running in Saleae Logic 2
    (or the tdm-test-harness) and plays the decoded PCM audio through
    a local audio device.
    """
    level = {0: logging.WARNING, 1: logging.INFO}.get(verbose, logging.DEBUG)
    logging.basicConfig(
        level=level,
        format='%(levelname)s: %(message)s',
    )
    log.info('tdm-audio-bridge %s', __version__)
    ctx.ensure_object(dict)
    ctx.obj['verbose'] = verbose


@cli.command()
@click.option('--host', default='127.0.0.1', help='HLA TCP server host')
@click.option('--port', default=4011, type=int, help='HLA TCP server port')
@click.option('--output', 'output_device', default=None,
              help='Audio output device (name substring or index)')
@click.option('--latency', default='high',
              type=click.Choice(['low', 'high']),
              help='Audio latency setting')
@click.option('--no-reconnect', is_flag=True,
              help='Exit on disconnect instead of reconnecting')
@click.help_option('-h', '--help')
def listen(host, port, output_device, latency, no_reconnect):
    """Connect to the HLA and play audio.

    Waits for the HLA TCP server, auto-configures from the handshake
    (sample rate, channels, bit depth), and plays through the selected
    output device.

    Examples:

        tdm-audio-bridge listen

        tdm-audio-bridge listen --port 4012 --output "VB-Cable"

        tdm-audio-bridge listen --output 5 --latency low
    """
    # Resolve output device early so we fail fast if invalid
    device_idx = None
    try:
        if output_device is not None:
            device_idx = find_device(output_device)
            dev_info = list_output_devices()
            name = next((d['name'] for d in dev_info if d['index'] == device_idx), '?')
            click.echo(f"Output device: [{device_idx}] {name}")
    except (ValueError, RuntimeError) as e:
        click.echo(click.style(f"Error: {e}", fg='red'), err=True)
        sys.exit(1)

    player = None
    player_lock = threading.Lock()
    stop = threading.Event()

    def on_handshake(handshake):
        nonlocal player
        click.echo(f"Connected: {handshake.channels}ch, "
                   f"{handshake.sample_rate}Hz, "
                   f"{handshake.bit_depth}-bit")
        click.echo(f"Slots: {handshake.slot_list}")

        with player_lock:
            if player is not None:
                player.stop()
            player = Player(handshake, device=device_idx, latency=latency)
            player.start()

        click.echo("Buffering...")

    playing_announced = False

    def on_data(data):
        nonlocal playing_announced
        with player_lock:
            if player is not None:
                player.feed(data)
                if not playing_announced and player.is_playing:
                    playing_announced = True
                    click.echo("Playing...")

    def on_disconnect():
        nonlocal player
        with player_lock:
            if player is not None:
                player.stop()
                player = None
        if no_reconnect:
            click.echo("Disconnected.")
            stop.set()
        else:
            click.echo("Disconnected. Reconnecting...")

    client = StreamClient(
        host=host,
        port=port,
        on_handshake=on_handshake,
        on_data=on_data,
        on_disconnect=on_disconnect,
        reconnect=not no_reconnect,
    )

    def sigint_handler(sig, frame):
        click.echo("\nStopping...")
        stop.set()

    signal.signal(signal.SIGINT, sigint_handler)

    click.echo(f"Connecting to {host}:{port}...")
    client.start()

    # Block until stopped
    stop.wait()
    client.stop()
    with player_lock:
        if player is not None:
            player.stop()
    click.echo("Shut down.")


@cli.command()
@click.help_option('-h', '--help')
def devices():
    """List available audio output devices.

    Shows device index, name, and maximum output channel count.
    Use the index or name with --output in the listen command.
    """
    try:
        devs = list_output_devices()
    except RuntimeError as e:
        click.echo(click.style(f"Error: {e}", fg='red'), err=True)
        sys.exit(1)
    if not devs:
        click.echo("No output devices found.")
        return

    click.echo("Available output devices:\n")
    for d in devs:
        click.echo(f"  [{d['index']:2d}] {d['name']} ({d['max_channels']} ch)")
    click.echo("")
    click.echo("Use --output <index> or --output <name> with 'listen'.")


@cli.command('gui')
@click.option('--host', default='127.0.0.1', help='HLA TCP server host')
@click.option('--port', default=4011, type=int, help='HLA TCP server port')
@click.help_option('-h', '--help')
def gui_cmd(host, port):
    """Launch the graphical interface.

    Opens a window for connecting to the HLA and playing audio without
    a terminal. All settings are configured through the GUI.
    """
    from .gui import launch
    try:
        launch(host=host, port=port)
    except RuntimeError as e:
        click.echo(click.style(f"Error: {e}", fg='red'), err=True)
        sys.exit(1)


def main():
    cli()


if __name__ == '__main__':
    main()
