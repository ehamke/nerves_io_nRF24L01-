defmodule Nerves.IO.Supervisor do

  use Supervisor

  @moduledoc """
  Documentation for RadioManager.
  """

  @doc """
  Hello world.

  ## Examples

      iex> RadioManager.hello
      :world

  """

   def start_link(opts \\ []) do
      Supervisor.start_link(__MODULE__, :ok, opts)
   end

   def init(_) do
       children = [
           {Nerves.IO.RF24, name: RF24}
       ]
       Supervisor.init(children, strategy:  :one_for_one)
   end

end
