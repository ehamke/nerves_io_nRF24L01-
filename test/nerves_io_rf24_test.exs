defmodule NERVES_RF24Test do
  use ExUnit.Case

{:ok, radio1} = Nerves.IO.RF24.start_link()

#  def port1() do
 #   System.get_env("NERVES_RF24")
 # end

  test "simple open and close", %{radio1: radio1} do
	assert :ok = Nerves.IO.RF24.open(radio1, "radio 1", [speed: 0  , CRC_length: 2 , PA_level: 1])
	assert :ok = Nerves.IO.RF24.close(radio1)
    end
end


