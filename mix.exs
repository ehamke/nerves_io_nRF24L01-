defmodule NERVES_RF24.Mixfile do
  use Mix.Project

  def project do
    [
      app: :nerves_io_rf24,
      version: "0.1.0",
      elixir: "~> 1.5",
      start_permanent: Mix.env == :prod,
      name: "nerves_io_rf24",
      description: description(),
      package: package(),
      source_url: "https://github.com/ehamke/nerves_io_nRF24L01-",
      compilers: [:elixir_make] ++ Mix.compilers,
      make_clean: ["clean"],
      build_embedded: Mix.env == :prod,
      start_permanent: Mix.env == :prod,
      deps: deps()
    ]
  end

  # Run "mix help compile.app" to learn about applications.
  def application do
    [
      extra_applications: [:logger]
    ]
  end

  defp description do
  """
  Elixir access to the nRF24L01+ over SPI
  """
  end

  defp package do
    %{files: ["lib", "src/*.[ch]", "mix.exs", "README.md", "LICENSE", "Makefile"],
      maintainers: ["Eric Hamke"],
      licenses: ["Debian"],
      links: %{"GitHub" => "https://github.com/ehamke/nerves_io_nRF24L01-"}}
  end

  # Run "mix help deps" to learn about dependencies.
  defp deps do
    [
      {:elixir_make, "~> 0.4.1"}
    ]
  end
end
