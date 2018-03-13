defmodule Nerves.IO.RF24 do
  @moduledoc """
   Worker module that spawns the RF24L01 port process and handles all communication

   ## Examples

      iex> NERVES_RF24.hello
      :world

  """
  use GenServer

  require Logger

  @type radio_option ::
          {:active, boolean}
          | {:speed, 0..3}
          | {:CRC_Length, 0..2}
          | {:PA_level, 0..2}

 #  def start_link(callback) do
 #    GenServer.start_link(__MODULE__, [callback], name: __MODULE__)
 # end

  @spec start_link([term]) :: {:ok, pid} | {:error, term}
  def start_link(opts \\ []) do
    GenServer.start_link(__MODULE__, [], opts)
  end

  defmodule State do
    @moduledoc false
    defstruct port: nil, callback: nil
  end

  def init([]) do
    executable = :code.priv_dir(:nerves_io_rf24) ++ '/rf24'
    args()
    port = Port.open({:spawn_executable, executable},
                     [{:args, args()},
                      {:packet, 2},
                      :use_stdio,
                      :binary,
                      :exit_status])
    state = %State{port: port}
    {:ok, state}
  end

  def handle_info({port, {:data, data}}, state = %State{port: port}) do
    cmd = :erlang.binary_to_term(data)
    IO.inspect cmd
    # handle_cmd(cmd, state)
    {:noreply, state}
  end

  def handle_info({port, {:exit_status, 1}}, state = %State{port: port}) do
    {:stop, {:error, :port_failure}, %State{state | port: nil}}
  end

  def handle_info({port, {:exit_status, s}}, state = %State{port: port}) when s > 1 do
    # restart after 2 sec
    {:noreply, %State{state | port: nil}, 2000}
  end

  def handle_info(:timeout, state = %State{port: nil}) do
    {:noreply, restart(state)}
  end

  def handle_info(unknown, state) do
    Logger.info "Huh? #{inspect unknown}"
    {:noreply, state}
  end

  @doc """
  Open a RF24 radio port.

  The following options are available:

    * `:active` - (`true` or `false`) specifies whether data is received as
       messages or by calling `read/2`. See discussion below.

    * `:speed` - (0,2) RF24_1MBPS (0), RF24_2MBPS (1), RF24_250KBPS (2)

    * `:CRC Length' - (0,2) RF24_CRC_DISABLED (0), RF24_CRC_8 (1), RF24_CRC_16 (2) 

    * `:PA Level` - (0, 4) RF24_PA_MIN (0), RF24_PA_LOW (1), RF24_PA_HIGH (2), RF24_PA_MAX (3), RF24_PA_ERROR (4)

    * `:framing` - (`module` or `{module, args}`) set the framing for data.
      The `module` must implement the `Nerves.UART.Framing` behaviour. See
      `Nerves.UART.Framing.None`, `Nerves.UART.Framing.Line`, and
      `Nerves.UART.Framing.FourByte`. The default is `Nerves.UART.Framing.None`.

    * `:rx_framing_timeout` - (milliseconds) this specifies how long incomplete
      frames will wait for the remainder to be received. Timed out partial
      frames are reported as `{:partial, data}`. A timeout of <= 0 means to
      wait forever.

  On error, `{:error, reason}` is returned.
  The following are some reasons:

    * `:enoent`  - the specified port couldn't be found
    * `:eagain`  - the port is already open
    * `:eacces`  - permission was denied when opening the port
  """

  @spec open(GenServer.server(), binary, [radio_option]) :: :ok | {:error, term}
  def open(pid, name, opts \\ []) do 
    GenServer.call(pid, {:open, name, opts})
    IO.puts "foo"
  end

  def handle_call({:open, pid}, name, opts )  do
    IO.puts "HiT"
    IO.puts opts
  end

  @doc """
  Change the RF24 radio configuration.
  """
  @spec configure(GenServer.server(), [radio_option]) :: :ok | {:error, term}
  def configure(pid, opts) do
    GenServer.call(pid, {:configure, opts})
  end

  @doc """
  Close the Radio port. The GenServer continues to run so that a port can
  be opened again.
  """
  @spec close(GenServer.server()) :: :ok | {:error, term}
  def close(pid) do
    GenServer.call(pid, :close)
  end

  defp restart(state) do
    executable = :code.priv_dir(:nerves_io_rf24) ++ '/rf24'
    args()
    port = Port.open({:spawn_executable, executable},
                     [{:args, args()},
                      {:packet, 2},
                      :use_stdio,
                      :binary,
                      :exit_status])
    %State{state | port: port}
  end

  defp handle_cmd({:tag, tag}, %State{callback: callback}) when is_function(callback) do
    callback.(tag)
  end

 # defp handle_cmd({:tag, tag}, %State{callback: {m, f}}) do
 #   apply(m, f, [tag])
 # end

  @env Mix.env
  defp args() do
    case @env do
      :test ->
        ["test"];
      _ ->
        [Integer.to_string(5000)]
    end
  end
end
