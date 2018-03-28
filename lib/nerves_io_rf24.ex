defmodule Nerves.IO.RF24 do
  use GenServer
  #require IEx; IEx.pry
  import IEx.Helpers
  require Logger
  

  # Many calls take timeouts for how long to wait for reading and writing
  # serial ports. This is the additional time added to the GenServer message passing
  # timeout so that the interprocess messaging timers don't hit before the
  # timeouts on the actual operations.
  @genserver_timeout_slack 100

  # There's a timeout when interacting with the port as well. If the port
  # doesn't respond by timeout + @port_timeout_slack, then there's something
  # wrong with it.
  @port_timeout_slack 50

  @moduledoc """
   Worker module that spawns the RF24L01 port process and handles all communication

   ## Examples

      iex> Nerves.IO.RF24.open

  """


  defmodule State do
    @moduledoc false

    # port: C port process
    # controlling_process: where events get sent
    # name: port name when opened
    # framing: framing behaviour
    # framing_state: framing behaviour's state
    # rx_framing_timeout: how long to wait for incomplete frames
    # queued_messages: queued messages when in passive mode
    # rx_framing_tref: frame completion timer
    # is_active: active or passive mode
    defstruct port: nil,
              controlling_process: nil,
              name: "RF24",
              framing: Nerves.IO.RF24.Framing.None,
              framing_state: nil,
              rx_framing_timeout: 0,
              queued_messages: [],
              rx_framing_tref: nil,
              is_active: true
  end

  @type radio_option ::
          {:active, boolean}
          | {:speed, 0..3}
          | {:CRC_Length, 0..2}
          | {:PA_level, 0..2}

  @doc """
  Start up a RF24 GenServer.
  """
  @spec start_link([term]) :: {:ok, pid} | {:error, term}
  def start_link(opts \\ []) do
    GenServer.start_link(__MODULE__, [], opts)
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
    IO.puts "Step 30"
    IO.inspect pid
    GenServer.call(pid, {:open, name, opts})
  end

  @doc """
  Close the radio port. The GenServer continues to run so that a port can
  be opened again.
  """
  @spec close(GenServer.server()) :: :ok | {:error, term}
  def close(pid) do
    IO.puts "Step 31"
    GenServer.call(pid, :close)
  end

  @doc """
  Change the radio port configuration after `open/3` has been called. See
  `open/3` for the valid options.
  """
  @spec configure(GenServer.server(), [radio_option]) :: :ok | {:error, term}
  def configure(pid, opts) do
    IO.puts "Step 33"
    IO.inspect opts
    GenServer.call(pid, {:configure, opts})
  end

#  @doc """
#  Send a continuous stream of zero bits for a duration in milliseconds.
#  By default, the zero bits are transmitted at least 0.25 seconds.
#
#  This is a convenience function for calling `set_break/2` to enable
#  the break signal, wait, and then turn it off.
#  """
#  @spec send_break(GenServer.server(), integer) :: :ok | {:error, term}
#  def send_break(pid, duration \\ 250) do
#    :ok = set_break(pid, true)
#    :timer.sleep(duration)
#    set_break(pid, false)
#  end

#  @doc """
#  Start or stop sending a break signal.
#  """
#  @spec set_break(GenServer.server(), boolean) :: :ok | {:error, term}
#  def set_break(pid, value) when is_boolean(value) do
#    GenServer.call(pid, {:set_break, value})
#  end

  @doc """
  Write data to the opened radio port. It's possible for the write to return before all
  of the data is actually transmitted. To wait for the data, call drain/1.

  This call blocks until all of the data to be written is in the operating
  system's internal buffers. If you're sending a lot of data on a slow link,
  supply a longer timeout to avoid timing out prematurely.

  Returns `:ok` on success or `{:error, reason}` if an error occurs.

  Typical error reasons:

    * `:ebadf` - the port is closed
  """
  @spec write(GenServer.server(), binary | [byte], integer) :: :ok | {:error, term}
  def write(pid, data, timeout) when is_binary(data) do
    GenServer.call(pid, {:write, data, timeout}, genserver_timeout(timeout))
  end

  def write(pid, data, timeout) when is_list(data) do
    write(pid, :erlang.iolist_to_binary(data), timeout)
  end

  @doc """
  Write data to the opened radio with a default 5 second timeout.
  """
  @spec write(GenServer.server(), binary | [byte]) :: :ok | {:error, term}
  def write(pid, data) do
    write(pid, data, 5000)
  end

  @doc """
  Read data from the radio. This call returns data as soon as it's available or
  after timing out.

  Returns `{:ok, binary}`, where `binary` is a binary data object that contains the
  read data, or `{:error, reason}` if an error occurs.

  Typical error reasons:

    * `:ebadf` - the radio port is closed
    * `:einval` - the radio port is in active mode
  """
  @spec read(GenServer.server(), integer) :: {:ok, binary} | {:error, term}
  def read(pid, timeout \\ 5000) do
    GenServer.call(pid, {:read, timeout}, genserver_timeout(timeout))
  end

  @doc """
  Waits until all data has been transmitted. See
  [tcdrain(3)](http://linux.die.net/man/3/tcdrain) for low level details on
  Linux or OSX. This is not implemented on Windows.
  """
  @spec drain(GenServer.server()) :: :ok | {:error, term}
  def drain(pid) do
    GenServer.call(pid, :drain)
  end

  @doc """
  Flushes the `:receive` buffer, the `:transmit` buffer, or `:both`.

  See [tcflush(3)](http://linux.die.net/man/3/tcflush) for low level details on
  Linux or OSX. This calls `PurgeComm` on Windows.
  """
  @spec flush(GenServer.server()) :: :ok | {:error, term}
  def flush(pid, direction \\ :both) do
    GenServer.call(pid, {:flush, direction})
  end

#  @doc """
#  Returns a map of signal names and their current state (true or false).
#  Signals include:
#
#    * `:dsr` - Data Set Ready
#    * `:dtr` - Data Terminal Ready
#    * `:rts` - Request To Send
#    * `:st`  - Secondary Transmitted Data
#    * `:sr`  - Secondary Received Data
#    * `:cts` - Clear To Send
#    * `:cd`  - Data Carrier Detect
#    * `:rng` - Ring Indicator
#  """
#  @spec signals(GenServer.server()) :: map | {:error, term}
#  def signals(pid) do
#    GenServer.call(pid, :signals)
#  end

#  @doc """
#  Set or clear the Data Terminal Ready signal.
#  """
#  @spec set_dtr(GenServer.server(), boolean) :: :ok | {:error, term}
#  def set_dtr(pid, value) when is_boolean(value) do
#    GenServer.call(pid, {:set_dtr, value})
#  end

#  @doc """
#  Set or clear the Request To Send signal.
#  """
#  @spec set_rts(GenServer.server(), boolean) :: :ok | {:error, term}
#  def set_rts(pid, value) when is_boolean(value) do
#    GenServer.call(pid, {:set_rts, value})
#  end

 @env Mix.env
  defp args() do
    case @env do
      :test ->
        ["test"];
      _ ->
        [Integer.to_string(5000)]
    end
  end

  # gen_server callbacks
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

  def handle_call({:open, name, opts}, {from_pid, _}, state) do
    IO.puts "Step 20"
    new_framing = Keyword.get(opts, :framing, nil)
    new_rx_framing_timeout = Keyword.get(opts, :rx_framing_timeout, state.rx_framing_timeout)
    is_active = Keyword.get(opts, :active, true)

    response = call_port(state, :open, {name, opts})

#    new_state =
#      change_framing(
#        %{
#          state
#          | name: name,
#            controlling_process: from_pid,
#            rx_framing_timeout: new_rx_framing_timeout,
#            is_active: is_active
#        },
#       new_framing
#      )

    new_state =%{
          state
          | name: name,
            controlling_process: from_pid,
            rx_framing_timeout: new_rx_framing_timeout,
            is_active: is_active
        }

#    %{state | one: "one"}
    {:reply, Map.fetch(state, :port), new_state}

end

  def handle_call(:close, _from, state) do
    # Clean up the C side
    response = call_port(state, :close, nil)

    # Clean up the Elixir side
    IO.puts "hi"
    new_framing_state = apply(state.framing, :flush, [:both, state.framing_state])

    new_state =
      handle_framing_timer(
        %{state 
            | name: nil, 
            framing_state: new_framing_state, 
            queued_messages: [],
            port: nil
            },  :ok)

    {:reply, response, new_state}
  end

  def handle_call({:read, _timeout}, _from, %{queued_messages: [message | rest]} = state) do
    # Return the queued response.
    new_state = %{state | queued_messages: rest}
    {:reply, {:ok, message}, new_state}
  end

  def handle_call({:read, timeout}, from, state) do
    call_time = System.monotonic_time(:millisecond)
    # Poll the serial port
    case call_port(state, :read, timeout, port_timeout(timeout)) do
      {:ok, <<>>} ->
        # Timeout
        {:reply, {:ok, <<>>}, state}

      {:ok, buffer} ->
        # More data
        {rc, messages, new_framing_state} =
          apply(state.framing, :remove_framing, [buffer, state.framing_state])

        new_state = handle_framing_timer(%{state | framing_state: new_framing_state}, rc)


        {rc, messages, new_framing_state} =
         apply(state.framing, :remove_framing, [buffer, state.framing_state])

        new_state = handle_framing_timer(%{state | framing_state: new_framing_state}, rc)

        if messages == [] do
          # If nothing, poll some more with reduced timeout
          elapsed = System.monotonic_time(:millisecond) - call_time
          retry_timeout = max(timeout - elapsed, 0)
          handle_call({:read, retry_timeout}, from, new_state)
        else
          # Return the first message
          [first_message | rest] = messages
          new_state = %{new_state | queued_messages: rest}
          {:reply, {:ok, first_message}, new_state}
        end

     response ->
        Error
       {:reply, response, state}
     end
  end

  def handle_call({:write, value, timeout}, _from, state) do
    {:ok, framed_data, new_framing_state} =
      apply(state.framing, :add_framing, [value, state.framing_state])

    response = call_port(state, :write, {framed_data, timeout}, port_timeout(timeout))
    new_state = %{state | framing_state: new_framing_state}
    {:reply, response, new_state}
  end

  def handle_call({:configure, opts}, _from, state) do
    new_framing = Keyword.get(opts, :framing, nil)
    new_rx_framing_timeout = Keyword.get(opts, :rx_framing_timeout, state.rx_framing_timeout)
    is_active = Keyword.get(opts, :active, state.is_active)

    state =
      change_framing(
        %{state | rx_framing_timeout: new_rx_framing_timeout, is_active: is_active},
        new_framing
      )

    response = call_port(state, :configure, opts)
    {:reply, response, state}
  end

  def handle_call(:drain, _from, state) do
    response = call_port(state, :drain, nil)
    {:reply, response, state}
  end

  def handle_call({:flush, direction}, _from, state) do
   IO.puts "step 60"
   fstate = apply(state.framing, :flush, [direction, state.framing_state])
   new_state = %{state | framing_state: fstate}
   response = call_port(new_state, :flush, direction)
   {:reply, response, new_state}
 end

  def handle_call(:signals, _from, state) do
    response = call_port(state, :signals, nil)
    {:reply, response, state}
  end

#  def handle_call({:set_dtr, value}, _from, state) do
#    response = call_port(state, :set_dtr, value)
#    {:reply, response, state}
#  end

#  def handle_call({:set_rts, value}, _from, state) do
#    response = call_port(state, :set_rts, value)
#    {:reply, response, state}
#  end

#  def handle_call({:set_break, value}, _from, state) do
#    response = call_port(state, :set_break, value)
#    {:reply, response, state}
#  end

  def terminate(_reason, state) do
     IO.puts("Going to terminate: #{inspect(_reason)}")

     Process.exit(state.controlling_process,:kill)

  end

  def handle_info({_, {:data, <<?n, message::binary>>}}, state) do
    IO.puts "step 15"
    msg = :erlang.binary_to_term(message)
    handle_port(msg, state)
  end

  def handle_info(:rx_framing_timed_out, state) do
    {:ok, messages, new_framing_state} =
      apply(state.framing, :frame_timeout, [state.framing_state])

    new_state =
      notify_timedout_messages(
        %{state | rx_framing_tref: nil, framing_state: new_framing_state},
        messages
      )

    {:noreply, new_state}
  end

   def handle_info({port, {:data, data}}, state = %State{port: port}) do
      IO.puts "step 14"
      cmd = :erlang.binary_to_term(data)
      IO.inspect cmd
      # handle_cmd(cmd, state)
      {:noreply, state}
    end

  def handle_info({port,{:exit_status,ref}}, state) do
     Port.flush(port)
     Logger.info "Exit code #{inspect ref}"
  end

 def handle_info(unknown, state) do
   IO.puts "elem 0:"
   IO.inspect elem(unknown,0)
   IO.puts "elem 1:"
   IO.inspect elem(unknown,1)
   IO.puts "elem 1.0"
   IO.inspect elem(elem(unknown, 1),0)
   if is_binary(elem(elem(unknown, 1),1)) do
            Logger.info "Huh? #{inspect :erlang.binary_to_term(elem(elem(unknown, 1),1))}"
   else
           Logger.info "Huh? #{inspect elem(elem(unknown, 1),1)}"
   end
   {:noreply, unknown}
 end

  defp notify_timedout_messages(%{is_active: true, controlling_process: dest} = state, messages)
       when dest != nil do
    Enum.each(messages, &report_message(state, &1))
    state
  end

  defp notify_timedout_messages(%{is_active: false} = state, messages) do
    # IO.puts("Queuing... #{inspect(messages)}")
    new_queued_messages = state.queued_messages ++ messages
    %{state | queued_messages: new_queued_messages}
  end

  defp notify_timedout_messages(state, _messages), do: state

  defp change_framing(state, nil), do: state

  defp change_framing(state, framing_mod) when is_atom(framing_mod) do
    IO.puts "step 41"
    change_framing(state, {framing_mod, []})
  end

  defp change_framing(state, {framing_mod, framing_args}) do
     IO.puts "step 42"
     IO.inspect framing_mod
     IO.inspect framing_args
     IO.inspect state
     {:ok, framing_state} = apply(framing_mod, :init, [framing_args])
    %{state | framing: framing_mod, framing_state: framing_state}
  end

  defp call_port(state, command, arguments, timeout \\ 4000) do
    IO.inspect command
    IO.inspect arguments
    IO.inspect state
    msg = {command, arguments}
    send(state.port, {self(), {:command, :erlang.term_to_binary(msg)}})
    #Block until the response comes back since the C side
    # doesn't want to handle any queuing of requests. REVISIT
    receive do
       {_, {:data, <<?r, response::binary>>}} ->:erlang.binary_to_term(response)
       IO.inspect :erlang.binary_to_term(response)
   after
     timeout ->
        # Not sure how this can be recovered
        exit(:port_timed_out) 
   end
end

  defp handle_port({:notif, data}, state) when is_binary(data) do
    # IO.puts "Received data on port #{state.name}"
    {rc, messages, new_framing_state} =
      apply(state.framing, :remove_framing, [data, state.framing_state])

    new_state = handle_framing_timer(%{state | framing_state: new_framing_state}, rc)

    if state.controlling_process do
      Enum.each(messages, &report_message(new_state, &1))
    end

    {:noreply, new_state}
  end

  defp handle_port({:notif, data}, state) do
    # Report an error from the port
    if state.controlling_process do
      report_message(state, data)
    end

    {:noreply, state}
  end

  defp report_message(state, message) do
    event = {:nerves_uart, state.name, message}
    send(state.controlling_process, event)
  end

  defp genserver_timeout(timeout) do
    max(timeout + @genserver_timeout_slack, @genserver_timeout_slack)
  end

  defp port_timeout(timeout) do
    max(timeout + @port_timeout_slack, @port_timeout_slack)
  end

  # Stop the framing timer if active and a frame completed
  defp handle_framing_timer(%{rx_framing_tref: tref} = state, :ok) when tref != nil do
    _ = :timer.cancel(tref)
    %{state | rx_framing_tref: tref}
  end

  # Start the framing timer if ended on an incomplete frame
  defp handle_framing_timer(%{rx_framing_timeout: timeout} = state, :in_frame) when timeout > 0 do
    _ = if state.rx_framing_tref, do: :timer.cancel(state.rx_framing_tref)
    {:ok, tref} = :timer.send_after(timeout, :rx_framing_timed_out)
    %{state | rx_framing_tref: tref}
  end

  # Don't do anything with the framing timer for all other reasons
  defp handle_framing_timer(state, _rc), do: state
 
end
